#include "Poller.h"

bool SelectPoller::canWatch(socket_t fd) const {
#ifdef _WIN32
    (void)fd;
    return watched_.size() < FD_SETSIZE;
#else
    return fd < FD_SETSIZE;
#endif
}

void SelectPoller::add(socket_t fd) {
    watched_[fd] = Interest{};
}

void SelectPoller::remove(socket_t fd) {
    watched_.erase(fd);
}

void SelectPoller::setInterest(socket_t fd, bool read, bool write) {
    auto it = watched_.find(fd);
    if(it != watched_.end()) it->second = Interest{read, write};
}

int SelectPoller::wait(int timeout_ms, std::vector<Event>& ready) {
    ready.clear();

    // select() takes the sets afresh on every call and overwrites them with the
    // result, so they are rebuilt from the registrations each time.
    fd_set readfds, writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    socket_t max_fd = 0;
    for(const auto& [fd, interest] : watched_) {
        if(interest.read) FD_SET(fd, &readfds);
        if(interest.write) FD_SET(fd, &writefds);
        if(fd > max_fd) max_fd = fd;
    }

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    // The first argument is the highest socket number + 1 (Windows ignores it).
    int n = select(static_cast<int>(max_fd + 1), &readfds, &writefds, nullptr, &timeout);
    if(n < 0) return isInterrupted(lastSocketError()) ? 0 : -1;

    for(const auto& [fd, interest] : watched_) {
        bool readable = interest.read && FD_ISSET(fd, &readfds);
        bool writable = interest.write && FD_ISSET(fd, &writefds);
        if(readable || writable) ready.push_back({fd, readable, writable});
    }
    return static_cast<int>(ready.size());
}
