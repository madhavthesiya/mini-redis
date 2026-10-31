#pragma once
#include <algorithm>
#include <climits>
#include <cstddef>

// Portable sockets: Winsock on Windows, BSD sockets elsewhere. Everything the
// server needs that differs between the two is kept here.

#ifdef _WIN32
    // FD_SETSIZE must be defined BEFORE including winsock2.h.
    // Windows defaults to 64 sockets per fd_set; use Linux's 1024.
    // With MSVC, windows.h (included by winsock2.h) defines min and max as
    // macros, which break std::min and std::max in any code included after it.
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
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
    #include <fcntl.h>
    #include <cerrno>
    using socket_t = int;
    #define CLOSE_SOCKET close
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
#endif

// The error code of the last failed socket call: errno, or WSAGetLastError() on Windows.
inline int lastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

// The call was cut short by a signal and can simply be made again.
inline bool isInterrupted(int error) {
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

// A non-blocking call found nothing to do (no data to read, no room to write,
// no connection to accept). Not an error: try again when the poller says ready.
inline bool wouldBlock(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

// Makes recv, send and accept on the socket return at once instead of waiting.
// A server thread that waits on one socket cannot serve any other.
inline bool setNonBlocking(socket_t s) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// send() and recv() with one signature on both platforms (Winsock takes int
// sizes). They return the bytes transferred, 0 for end of stream (recv), or
// -1 with the reason in lastSocketError().
inline long long sendSome(socket_t s, const char* data, size_t len) {
#ifdef _WIN32
    return send(s, data, static_cast<int>(std::min<size_t>(len, INT_MAX)), 0);
#else
    return send(s, data, len, 0);
#endif
}

inline long long recvSome(socket_t s, char* buffer, size_t len) {
#ifdef _WIN32
    return recv(s, buffer, static_cast<int>(std::min<size_t>(len, INT_MAX)), 0);
#else
    return recv(s, buffer, len, 0);
#endif
}
