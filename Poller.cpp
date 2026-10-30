#include "Poller.h"

void SelectPoller::add(socket_t fd) {
    watched_[fd] = Interest{};
}

void SelectPoller::remove(socket_t fd) {
    watched_.erase(fd);
}

int SelectPoller::wait(int timeout_ms, std::vector<Event>& ready) {
    ready.clear();

    // select() takes the sets afresh on every call and overwrites them with the
    // result, so they are rebuilt from the registrations each time.
    fd_set readfds;
    FD_ZERO(&readfds);
    socket_t max_fd = 0;
    for(const auto& [fd, interest] : watched_) {
        if(interest.read) FD_SET(fd, &readfds);
        if(fd > max_fd) max_fd = fd;
    }

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    // The first argument is the highest socket number + 1 (Windows ignores it).
    int n = select(static_cast<int>(max_fd + 1), &readfds, nullptr, nullptr, &timeout);
    if(n < 0) return isInterrupted(lastSocketError()) ? 0 : -1;

    for(const auto& [fd, interest] : watched_) {
        if(interest.read && FD_ISSET(fd, &readfds)) ready.push_back({fd, true});
    }
    return static_cast<int>(ready.size());
}
