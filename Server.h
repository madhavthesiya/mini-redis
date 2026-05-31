#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// Cross-platform socket abstraction.
// FD_SETSIZE must be defined BEFORE including winsock2.h
// Windows defaults to 64 — too small for a real server.
#ifdef _WIN32
    #ifndef FD_SETSIZE
        #define FD_SETSIZE 1024
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>    // TCP_NODELAY
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <sys/select.h>
    using socket_t = int;
    #define CLOSE_SOCKET close
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
#endif

#include "InMemoryStorage.h"
#include "RespParser.h"

class Server {
public:
    Server(int port, InMemoryStorage& storage, AOFLogger* aof = nullptr);
    ~Server();

    bool start();       // Create socket, bind, listen
    void run();         // select() event loop — handles multiple clients concurrently
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

    int port_;
    socket_t listen_socket_ = INVALID_SOCKET;
    InMemoryStorage& storage_;
    bool running_ = false;

    AOFLogger* aof_ = nullptr;  // optional — for AOFREWRITE command

    // Per-client receive buffers — each client needs its own buffer
    // because TCP can deliver partial commands across multiple recv() calls.
    // Key = client socket, Value = accumulated receive buffer.
    std::unordered_map<socket_t, std::string> client_buffers_;
};
