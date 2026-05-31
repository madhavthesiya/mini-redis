#include <iostream>
#include "InMemoryStorage.h"
#include "AOFLogger.h"
#include "Server.h"

int main() {
    // AOF persistence — every write is crash-safe
    AOFLogger aof("appendonly.aof");
    InMemoryStorage storage(10000, &aof);   // 10k key capacity for server mode

    // Restore state from previous session
    storage.replayAOF();

    // Start TCP server on port 6379 (same as real Redis)
    Server server(6379, storage, &aof);
    if(!server.start()) {
        std::cerr << "Failed to start server. Is port 6379 in use?\n";
        std::cerr << "Try: mini_redis_server  (will use port 6379)\n";
        return 1;
    }

    // Block forever, handling client connections
    server.run();

    return 0;
}
