#include "Server.h"
#include <iostream>
#include <algorithm>

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
#endif

    // Create TCP socket
    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_socket_ == INVALID_SOCKET) {
        std::cerr << "socket() failed\n";
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

    running_ = true;
    std::cout << "Mini-Redis server listening on port " << port_ << "\n";
    std::cout << "Connect with: redis-cli -p " << port_ << "\n";
    std::cout << "I/O multiplexing: select() — single-threaded, concurrent clients\n";
    return true;
}

void Server::stop() {
    running_ = false;
    // Close all client sockets
    for(auto& [client, _] : client_buffers_) {
        CLOSE_SOCKET(client);
    }
    client_buffers_.clear();
    if(listen_socket_ != INVALID_SOCKET) {
        CLOSE_SOCKET(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

// ==================== I/O Multiplexing Event Loop ====================
//
// How select() works:
//   1. Build a set of sockets to monitor (listen + all clients)
//   2. Call select() — it blocks until at least one socket is ready
//   3. Check which sockets are ready:
//      - listen socket ready → new client connecting → accept()
//      - client socket ready → data available → recv() and process
//   4. Repeat
//
// Why single-threaded?
//   No locks needed, no race conditions, no deadlocks.
//   One thread handles hundreds of clients efficiently.
//   This is how Redis itself works — single-threaded event loop.

void Server::run() {
    while(running_) {
        // Step 1: Build the fd_set with all sockets to monitor
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_socket_, &readfds);

        socket_t max_fd = listen_socket_;
        for(const auto& [client, _] : client_buffers_) {
            FD_SET(client, &readfds);
            if(client > max_fd) max_fd = client;
        }

        // Step 2: Wait for any socket to become ready
        // Timeout of 1 second allows us to check the running_ flag periodically
        // (needed for clean shutdown via Ctrl+C)
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(static_cast<int>(max_fd + 1), &readfds,
                          nullptr, nullptr, &timeout);

        if(ready < 0) {
            if(running_) std::cerr << "select() error\n";
            continue;
        }
        if(ready == 0) continue;  // timeout — loop back and check running_

        // Step 3: Check which sockets are ready

        // 3a: New client connection?
        if(FD_ISSET(listen_socket_, &readfds)) {
            acceptNewClient();
        }

        // 3b: Data from existing clients?
        // IMPORTANT: copy the client list before iterating.
        // handleClientData may call removeClient, which modifies client_buffers_.
        // Iterating over a container while modifying it = undefined behavior.
        std::vector<socket_t> clients;
        clients.reserve(client_buffers_.size());
        for(const auto& [client, _] : client_buffers_) {
            clients.push_back(client);
        }

        for(socket_t client : clients) {
            // Check client still exists (might have been removed by a prior iteration)
            if(client_buffers_.count(client) && FD_ISSET(client, &readfds)) {
                handleClientData(client);
            }
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
        std::cerr << "accept() failed\n";
        return;
    }

    // Register client with an empty receive buffer
    client_buffers_[client] = "";

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
    std::cout << "Client connected: " << ip << ":"
              << ntohs(client_addr.sin_port)
              << " (total: " << client_buffers_.size() << ")\n";
}

void Server::removeClient(socket_t client) {
    CLOSE_SOCKET(client);
    client_buffers_.erase(client);
    std::cout << "Client disconnected (remaining: " << client_buffers_.size() << ")\n";
}

// ==================== Client Data Handling ====================

void Server::handleClientData(socket_t client) {
    char tmp[4096];
    int n = recv(client, tmp, sizeof(tmp), 0);

    if(n <= 0) {
        // n == 0: graceful disconnect
        // n < 0:  network error
        removeClient(client);
        return;
    }

    client_buffers_[client].append(tmp, n);
    std::string& buffer = client_buffers_[client];

    // Process all complete commands in the buffer
    while(!buffer.empty()) {
        auto [args, consumed] = RespParser::parse(buffer);
        if(consumed == 0) break;    // incomplete command — wait for more data
        buffer.erase(0, consumed);
        if(args.empty()) continue;

        std::string response = executeCommand(args);
        send(client, response.c_str(), static_cast<int>(response.size()), 0);

        // Handle QUIT — send response first, then disconnect
        if(!args.empty() && toUpper(args[0]) == "QUIT") {
            removeClient(client);
            return;  // buffer is gone, stop processing
        }
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
                           + std::to_string(client_buffers_.size()) + "\r\n";
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
        if(aof_) {
            auto commands = storage_.dumpState();
            aof_->rewrite(commands);
        }
        return RespParser::simpleString("OK");
    }

    // ---- Unknown command ----
    return RespParser::error("ERR unknown command '" + args[0] + "'");
}
