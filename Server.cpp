#include "Server.h"
#include <iostream>
#include <algorithm>
#include <csignal>

Server::Server(int port, InMemoryStorage& storage, AOFLogger* aof)
    : port_(port), storage_(storage), aof_(aof) {}

Server::~Server() {
    stop();
}

std::string Server::toUpper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

// ==================== Lifecycle ====================

bool Server::start() {
#ifdef _WIN32
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return false;
    }
#else
    // send() on a socket whose client has already disconnected raises SIGPIPE,
    // and its default action terminates the whole process: one client closing
    // early would take the server down for everyone. Ignoring it makes send()
    // fail with EPIPE instead, and the server removes that client.
    // Redis does the same at startup. (Windows has no SIGPIPE.)
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // Create TCP socket
    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_socket_ == INVALID_SOCKET) {
        std::cerr << "socket() failed\n";
        return false;
    }

    // Non-blocking, so accept() can never stall the loop: a client may give up
    // between the poller reporting its connection and the accept() call.
    if(!setNonBlocking(listen_socket_)) {
        std::cerr << "Could not make the listening socket non-blocking\n";
        CLOSE_SOCKET(listen_socket_);
        return false;
    }

    // Allow port reuse (avoid "address already in use" on restart)
    int opt = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    // Bind to port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if(bind(listen_socket_, reinterpret_cast<struct sockaddr*>(&addr),
            sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed — is port " << port_ << " already in use?\n";
        CLOSE_SOCKET(listen_socket_);
        return false;
    }

    // Mark as passive (server) — backlog of 16 pending connections
    if(listen(listen_socket_, 16) == SOCKET_ERROR) {
        std::cerr << "listen() failed\n";
        CLOSE_SOCKET(listen_socket_);
        return false;
    }

    poller_->add(listen_socket_);
    running_ = true;
    std::cout << "Mini-Redis server listening on port " << port_ << "\n";
    std::cout << "Connect with: redis-cli -p " << port_ << "\n";
    std::cout << "I/O multiplexing: select() — single-threaded, concurrent clients\n";
    return true;
}

void Server::stop() {
    running_ = false;
    // Close all client sockets
    for(auto& [fd, client] : clients_) {
        poller_->remove(fd);
        CLOSE_SOCKET(fd);
    }
    clients_.clear();
    if(listen_socket_ != INVALID_SOCKET) {
        poller_->remove(listen_socket_);
        CLOSE_SOCKET(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

// ==================== I/O Multiplexing Event Loop ====================
//
// How the loop works:
//   1. The listening socket and every client socket are registered with the
//      poller (select() today; see Poller.h)
//   2. wait() blocks until at least one of them is ready
//   3. Handle only the ready ones:
//      - listen socket ready → new client connecting → accept()
//      - client readable → recv(), run its complete commands, send the replies
//      - client writable → send more of the replies it still has waiting
//   4. Repeat
//
// Every socket is non-blocking, so no call ever waits for one client: if a
// reply can't be sent yet, it stays in that client's buffer and the loop asks
// the poller to report when the socket can take more.
//
// Why single-threaded?
//   Commands run one at a time, so no two commands can race each other.
//   Redis's main thread uses the same model, but waits with epoll/kqueue,
//   which return only the ready sockets; select() scans every socket each call.

void Server::run() {
    std::vector<Poller::Event> ready;
    while(running_) {
        // The 1-second timeout makes the loop re-check running_ regularly.
        // Nothing clears it on Ctrl+C yet (no signal handler), so Ctrl+C
        // still terminates the process directly.
        if(poller_->wait(1000, ready) < 0) {
            if(running_) std::cerr << "select() error\n";
            continue;
        }

        // `ready` is a separate list, so removing a client below while going
        // through it is safe.
        for(const auto& event : ready) {
            if(event.fd == listen_socket_) {
                acceptNewClient();
                continue;
            }
            // Each check also skips a client removed earlier in this pass
            if(event.readable && clients_.count(event.fd)) handleReadable(event.fd);
            if(event.writable && clients_.count(event.fd)) serveClient(event.fd);
        }
    }
}

// ==================== Connection Management ====================

void Server::acceptNewClient() {
    struct sockaddr_in client_addr{};
    int addr_len = sizeof(client_addr);

    socket_t client = accept(listen_socket_,
        reinterpret_cast<struct sockaddr*>(&client_addr),
        reinterpret_cast<socklen_t*>(&addr_len));

    if(client == INVALID_SOCKET) {
        // "Would block": the client gave up before we got to it. Not an error.
        int error = lastSocketError();
        if(!wouldBlock(error) && !isInterrupted(error)) {
            std::cerr << "accept() failed (error " << error << ")\n";
        }
        return;
    }

    // Linux sockets don't inherit O_NONBLOCK from the listening socket
    if(!setNonBlocking(client)) {
        std::cerr << "Could not make a client socket non-blocking\n";
        CLOSE_SOCKET(client);
        return;
    }

    // Disable Nagle's algorithm — send responses immediately, don't batch.
    // Without this, TCP waits up to 40ms to coalesce small packets.
    // For a request-response protocol like Redis, that delay is pure waste.
    int flag = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));

    // Register client with empty buffers, watched for readability
    clients_[client];
    poller_->add(client);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
    std::cout << "Client connected: " << ip << ":"
              << ntohs(client_addr.sin_port)
              << " (total: " << clients_.size() << ")\n";
}

void Server::removeClient(socket_t fd) {
    poller_->remove(fd);
    CLOSE_SOCKET(fd);
    clients_.erase(fd);
    std::cout << "Client disconnected (remaining: " << clients_.size() << ")\n";
}

// ==================== Client Data Handling ====================

void Server::handleReadable(socket_t fd) {
    Client& client = clients_.at(fd);
    char tmp[4096];
    long long n = recvSome(fd, tmp, sizeof(tmp));

    if(n == 0) {            // the client closed the connection
        removeClient(fd);
        return;
    }
    if(n < 0) {
        int error = lastSocketError();
        if(wouldBlock(error) || isInterrupted(error)) return;   // nothing to read after all
        removeClient(fd);   // connection error, e.g. reset by the client
        return;
    }
    client.in.append(tmp, static_cast<size_t>(n));

    // The parser limits each part of a command; this caps the total a client can
    // make the server hold (Redis's client-query-buffer-limit default).
    if(client.in.size() > kMaxQueryBuffer) {
        std::cerr << "Closing client: input exceeds the "
                  << (kMaxQueryBuffer >> 20) << " MB query buffer limit\n";
        removeClient(fd);
        return;
    }
    serveClient(fd);
}

// Sends what the client has waiting, then runs its next commands, until it has
// no complete command left or its socket can't take more. New commands run only
// once earlier replies are all sent, so a client that doesn't read gets no
// further work done for it: its replies stay bounded and TCP flow control slows
// it down.
void Server::serveClient(socket_t fd) {
    Client& client = clients_.at(fd);
    while(true) {
        if(client.out_sent < client.out.size()) {
            SendStatus status = sendReplies(fd, client);
            if(status == SendStatus::Failed) {     // the client has gone
                removeClient(fd);
                return;
            }
            if(status == SendStatus::Blocked) break;   // the poller will say when there is room
        }
        if(client.close_after_reply) {             // QUIT or protocol error, reply sent
            removeClient(fd);
            return;
        }
        // Commands may be waiting in `in` even if no new data arrives: the client
        // may have sent everything and now waits for these replies.
        runCommands(client);
        if(client.out.empty()) break;              // no complete command left
    }
    updateInterest(fd, client);
}

// Runs whole commands from the client's input and appends their replies, until
// the input has no complete command left or kOutputHighWater bytes of replies
// are waiting.
void Server::runCommands(Client& client) {
    // Parse from a moving position and drop the used bytes once at the end.
    // Erasing each command from the front would move all the bytes behind it,
    // once per command: quadratic in the number of commands in one read.
    size_t pos = 0;
    while(client.out.size() < kOutputHighWater && !client.close_after_reply) {
        auto parsed = RespParser::parse(std::string_view(client.in).substr(pos));
        if(parsed.status == RespParser::ParseStatus::NeedMore) break;  // wait for more data
        if(parsed.status == RespParser::ParseStatus::Error) {
            // As Redis does: say why, then disconnect. Nothing after the bad bytes
            // can be trusted, because where the bad command ends is unknown.
            client.out += RespParser::error("ERR Protocol error: " + parsed.error);
            client.close_after_reply = true;
            client.in.clear();
            return;
        }
        pos += parsed.consumed;
        const auto& args = parsed.args;
        if(args.empty()) continue;

        client.out += executeCommand(args);

        // QUIT: send its reply, then disconnect; commands after it don't run
        if(toUpper(args[0]) == "QUIT") {
            client.close_after_reply = true;
            client.in.clear();
            return;
        }
    }
    client.in.erase(0, pos);
}

// Sends as much of the waiting replies as the socket takes. A partial send is
// normal: the rest goes when the poller reports the socket writable again.
Server::SendStatus Server::sendReplies(socket_t fd, Client& client) {
    while(client.out_sent < client.out.size()) {
        long long n = sendSome(fd, client.out.data() + client.out_sent,
                               client.out.size() - client.out_sent);
        if(n > 0) {
            client.out_sent += static_cast<size_t>(n);
            continue;
        }
        if(n < 0) {
            int error = lastSocketError();
            if(isInterrupted(error)) continue;
            if(wouldBlock(error)) return SendStatus::Blocked;   // socket buffer full
        }
        return SendStatus::Failed;   // e.g. EPIPE or ECONNRESET
    }
    client.out.clear();
    client.out_sent = 0;
    return SendStatus::Done;
}

// Reads while nothing waits to be sent; waits for writability while something does.
void Server::updateInterest(socket_t fd, Client& client) {
    bool pending = client.out_sent < client.out.size();
    bool want_read = !pending && !client.close_after_reply;
    bool want_write = pending;
    if(want_read != client.want_read || want_write != client.want_write) {
        poller_->setInterest(fd, want_read, want_write);
        client.want_read = want_read;
        client.want_write = want_write;
    }
}

// ==================== Command Execution ====================
//
// Maps RESP commands to InMemoryStorage methods.
// Each command has its own response format — not generic.

std::string Server::executeCommand(const std::vector<std::string>& args) {
    if(args.empty()) return RespParser::error("ERR empty command");

    std::string cmd = toUpper(args[0]);

    // ---- Redis-cli startup commands ----

    if(cmd == "COMMAND") {
        return RespParser::emptyArray();
    }
    if(cmd == "CLIENT") {
        return RespParser::simpleString("OK");
    }
    if(cmd == "CONFIG" && args.size() >= 2 && toUpper(args[1]) == "GET") {
        return RespParser::emptyArray();
    }
    if(cmd == "PING") {
        return RespParser::simpleString("PONG");
    }
    if(cmd == "QUIT") {
        return RespParser::simpleString("OK");
    }
    if(cmd == "INFO") {
        std::string info = "# Server\r\nmini_redis_version:2.0\r\n"
                           "# Clients\r\nconnected_clients:"
                           + std::to_string(clients_.size()) + "\r\n";
        return RespParser::bulkString(info);
    }

    // ---- String commands ----

    if(cmd == "SET") {
        if(args.size() < 3) return RespParser::error("ERR wrong number of arguments for 'set' command");
        if(args.size() >= 5 && toUpper(args[3]) == "EX") {
            try {
                int ttl = std::stoi(args[4]);
                auto r = storage_.set(args[1], args[2], ttl);
                return r.success ? RespParser::simpleString("OK")
                                 : RespParser::error("ERR set failed");
            } catch(...) {
                return RespParser::error("ERR value is not an integer");
            }
        }
        auto r = storage_.set(args[1], args[2]);
        return r.success ? RespParser::simpleString("OK")
                         : RespParser::error("ERR set failed");
    }

    if(cmd == "GET") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for 'get' command");
        auto r = storage_.get(args[1]);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return r.success ? RespParser::bulkString(r.value) : RespParser::nullBulk();
    }

    if(cmd == "SETEX") {
        if(args.size() != 4) return RespParser::error("ERR wrong number of arguments for 'setex' command");
        try {
            int ttl = std::stoi(args[2]);
            auto r = storage_.set(args[1], args[3], ttl);
            return r.success ? RespParser::simpleString("OK")
                             : RespParser::error("ERR setex failed");
        } catch(...) {
            return RespParser::error("ERR value is not an integer");
        }
    }

    // ---- Key commands ----

    if(cmd == "DEL") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for 'del' command");
        auto r = storage_.del(args[1]);
        return RespParser::integer(r.success ? 1 : 0);
    }

    if(cmd == "EXISTS") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for 'exists' command");
        auto r = storage_.exists(args[1]);
        return RespParser::integer(r.success ? 1 : 0);
    }

    if(cmd == "TYPE") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for 'type' command");
        auto r = storage_.type(args[1]);
        return RespParser::simpleString(r.value);
    }

    if(cmd == "EXPIRE") {
        if(args.size() != 3) return RespParser::error("ERR wrong number of arguments for 'expire' command");
        try {
            int ttl = std::stoi(args[2]);
            auto r = storage_.expire(args[1], ttl);
            return RespParser::integer(r.success ? 1 : 0);
        } catch(...) {
            return RespParser::error("ERR value is not an integer");
        }
    }

    if(cmd == "TTL") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for 'ttl' command");
        auto r = storage_.ttl(args[1]);
        if(!r.success) return RespParser::integer(-2);
        return RespParser::integer(std::stoi(r.value));
    }

    // ---- List commands ----

    if(cmd == "LPUSH" || cmd == "RPUSH") {
        if(args.size() < 3) return RespParser::error("ERR wrong number of arguments for '" + cmd + "' command");
        std::vector<std::string> vals(args.begin() + 2, args.end());
        auto r = (cmd == "LPUSH") ? storage_.lpush(args[1], vals) : storage_.rpush(args[1], vals);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RespParser::integer(std::stoi(r.value));
    }

    if(cmd == "LPOP" || cmd == "RPOP") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for '" + cmd + "' command");
        auto r = (cmd == "LPOP") ? storage_.lpop(args[1]) : storage_.rpop(args[1]);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return r.success ? RespParser::bulkString(r.value) : RespParser::nullBulk();
    }

    if(cmd == "LRANGE") {
        if(args.size() != 4) return RespParser::error("ERR wrong number of arguments for 'lrange' command");
        try {
            int start = std::stoi(args[2]);
            int stop = std::stoi(args[3]);
            auto r = storage_.lrange(args[1], start, stop);
            if(r.error == ErrorCode::WRONG_TYPE)
                return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
            return RespParser::array(r.values);
        } catch(...) {
            return RespParser::error("ERR value is not an integer");
        }
    }

    if(cmd == "LLEN") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for 'llen' command");
        auto r = storage_.llen(args[1]);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RespParser::integer(std::stoi(r.value));
    }

    // ---- Set commands ----

    if(cmd == "SADD" || cmd == "SREM") {
        if(args.size() < 3) return RespParser::error("ERR wrong number of arguments for '" + cmd + "' command");
        std::vector<std::string> members(args.begin() + 2, args.end());
        auto r = (cmd == "SADD") ? storage_.sadd(args[1], members) : storage_.srem(args[1], members);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RespParser::integer(std::stoi(r.value));
    }

    if(cmd == "SMEMBERS") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for 'smembers' command");
        auto r = storage_.smembers(args[1]);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RespParser::array(r.values);
    }

    if(cmd == "SISMEMBER") {
        if(args.size() != 3) return RespParser::error("ERR wrong number of arguments for 'sismember' command");
        auto r = storage_.sismember(args[1], args[2]);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RespParser::integer(std::stoi(r.value));
    }

    if(cmd == "SCARD") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for 'scard' command");
        auto r = storage_.scard(args[1]);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RespParser::integer(std::stoi(r.value));
    }

    // ---- Hash commands ----

    if(cmd == "HSET") {
        if(args.size() < 4 || args.size() % 2 != 0)
            return RespParser::error("ERR wrong number of arguments for 'hset' command");
        std::vector<std::pair<std::string,std::string>> fields;
        for(size_t i = 2; i < args.size(); i += 2) {
            fields.emplace_back(args[i], args[i+1]);
        }
        auto r = storage_.hset(args[1], fields);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RespParser::integer(std::stoi(r.value));
    }

    if(cmd == "HGET") {
        if(args.size() != 3) return RespParser::error("ERR wrong number of arguments for 'hget' command");
        auto r = storage_.hget(args[1], args[2]);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return r.success ? RespParser::bulkString(r.value) : RespParser::nullBulk();
    }

    if(cmd == "HDEL") {
        if(args.size() < 3) return RespParser::error("ERR wrong number of arguments for 'hdel' command");
        std::vector<std::string> fields(args.begin() + 2, args.end());
        auto r = storage_.hdel(args[1], fields);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RespParser::integer(std::stoi(r.value));
    }

    if(cmd == "HGETALL") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for 'hgetall' command");
        auto r = storage_.hgetall(args[1]);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RespParser::array(r.values);
    }

    if(cmd == "HLEN") {
        if(args.size() != 2) return RespParser::error("ERR wrong number of arguments for 'hlen' command");
        auto r = storage_.hlen(args[1]);
        if(r.error == ErrorCode::WRONG_TYPE)
            return RespParser::error("WRONGTYPE Operation against a key holding the wrong kind of value");
        return RespParser::integer(std::stoi(r.value));
    }

    // ---- Persistence ----

    if(cmd == "SAVE") {
        auto r = storage_.saveToFile("dump.rdb");
        return r.success ? RespParser::simpleString("OK") : RespParser::error("ERR save failed");
    }

    if(cmd == "BGREWRITEAOF" || cmd == "AOFREWRITE") {
        if(!aof_) return RespParser::error("ERR AOF is disabled (start the server with --appendonly yes)");
        auto commands = storage_.dumpState();
        aof_->rewrite(commands);
        return RespParser::simpleString("OK");
    }

    // ---- Unknown command ----
    // The name comes from the client and can be up to 512 MB: quote at most 128 bytes, as Redis does.
    return RespParser::error("ERR unknown command '" + args[0].substr(0, 128) + "'");
}
