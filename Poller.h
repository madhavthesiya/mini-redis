#pragma once
#include "Socket.h"
#include <unordered_map>
#include <vector>

// Readiness notification for many sockets, which lets one thread serve many
// clients: wait until some sockets are ready, handle just those, repeat.
//
// Sockets are registered once, with the events of interest. That suits both
// select(), which rebuilds its sets from the registrations on every wait, and
// epoll/kqueue, which keep them in the kernel, so the event loop does not need
// to know which one it is using.
class Poller {
public:
    struct Event {
        socket_t fd;
        bool readable;   // data, a connection to accept, end of stream or an error
        bool writable;   // send() can take more data
    };

    virtual ~Poller() = default;

    virtual void add(socket_t fd) = 0;      // start watching, for readability
    virtual void remove(socket_t fd) = 0;   // stop watching; call before closing the socket

    // Which events to report for a watched socket. Ask for writability only while
    // there is something to send: a socket is writable almost all the time, so
    // watching an idle one would wake the loop for nothing.
    virtual void setInterest(socket_t fd, bool read, bool write) = 0;

    // Waits up to timeout_ms for watched sockets to become ready and fills `ready`
    // with them. Returns how many are ready: 0 on timeout or when a signal cut the
    // wait short, -1 on any other error.
    virtual int wait(int timeout_ms, std::vector<Event>& ready) = 0;
};

// select(): available everywhere. Each wait costs O(number of watched sockets),
// and fd_set has a fixed capacity, FD_SETSIZE.
class SelectPoller : public Poller {
public:
    void add(socket_t fd) override;
    void remove(socket_t fd) override;
    void setInterest(socket_t fd, bool read, bool write) override;
    int wait(int timeout_ms, std::vector<Event>& ready) override;

private:
    struct Interest {
        bool read = true;
        bool write = false;
    };
    std::unordered_map<socket_t, Interest> watched_;
};
