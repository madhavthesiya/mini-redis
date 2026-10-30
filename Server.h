#pragma once
#include "Socket.h"     // first: on Windows it must set FD_SETSIZE before winsock2.h is included
#include "Poller.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "InMemoryStorage.h"
#include "RespParser.h"

class Server {
public:
    Server(int port, InMemoryStorage& storage, AOFLogger* aof = nullptr);
    ~Server();

    bool start();       // Create socket, bind, listen
    void run();         // Event loop — handles multiple clients concurrently
    void stop();

private:
    // I/O multiplexing handlers
    void acceptNewClient();                 // New connection on listen socket
    void handleClientData(socket_t client); // Data ready on client socket
    void removeClient(socket_t client);     // Cleanup on disconnect

    // Execute a parsed command and return the RESP-formatted response
    std::string executeCommand(const std::vector<std::string>& args);

    // Convert string to uppercase (for case-insensitive command matching)
    static std::string toUpper(const std::string& s);

    // Most a client's incomplete command may occupy (Redis's client-query-buffer-limit)
    static constexpr size_t kMaxQueryBuffer = size_t(1) << 30;

    int port_;
    socket_t listen_socket_ = INVALID_SOCKET;
    InMemoryStorage& storage_;
    bool running_ = false;

    AOFLogger* aof_ = nullptr;  // optional — for AOFREWRITE command

    // Tells the event loop which sockets are ready
    std::unique_ptr<Poller> poller_ = std::make_unique<SelectPoller>();

    // Per-client receive buffers — each client needs its own buffer
    // because TCP can deliver partial commands across multiple recv() calls.
    // Key = client socket, Value = accumulated receive buffer.
    std::unordered_map<socket_t, std::string> client_buffers_;
};
