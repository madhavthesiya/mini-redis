#pragma once

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
