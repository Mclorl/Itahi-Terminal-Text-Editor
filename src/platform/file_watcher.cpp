#include "platform/file_watcher.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/inotify.h>
#endif

namespace {

    constexpr int tick_ms = 1000;
    // changes come in bursts (a save is a new file, a write and a rename; git
    // touches many files): wait until it has been quiet this long.
    constexpr int quiet_ms = 120;

}  // namespace

FileWatcher::FileWatcher(std::function<void()> on_change, std::function<void()> on_tick)
    : on_change_(std::move(on_change)), on_tick_(std::move(on_tick)) {
#if defined(__linux__)
    inotify_fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
#endif
    if (::pipe(wake_fd_) == 0) {
        for (const int fd : wake_fd_) {
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);
            ::fcntl(fd, F_SETFL, O_NONBLOCK);
        }
    } else {
        wake_fd_[0] = wake_fd_[1] = -1;
    }
    thread_ = std::thread([this] { run(); });
}

FileWatcher::~FileWatcher() {
    stop_ = true;
    if (wake_fd_[1] >= 0) {
        const char byte = 'x';
        (void)!::write(wake_fd_[1], &byte, 1);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    for (const int fd : {inotify_fd_, wake_fd_[0], wake_fd_[1]}) {
        if (fd >= 0) {
            ::close(fd);
        }
    }
}

void FileWatcher::watch(const std::vector<std::string>& folders) {
#if defined(__linux__)
    if (inotify_fd_ < 0) {
        return;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    // no longer wanted
    for (auto it = watched_.begin(); it != watched_.end();) {
        if (std::find(folders.begin(), folders.end(), it->first) == folders.end()) {
            ::inotify_rm_watch(inotify_fd_, it->second);
            it = watched_.erase(it);
        } else {
            ++it;
        }
    }
    // new
    constexpr uint32_t events = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE |
                                IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;
    for (const std::string& folder : folders) {
        if (watched_.count(folder) != 0) {
            continue;
        }
        const int watch = ::inotify_add_watch(inotify_fd_, folder.c_str(), events);
        if (watch >= 0) {
            watched_[folder] = watch;
        }
    }
#else
    (void)folders;
#endif
}

void FileWatcher::run() {
    using clock = std::chrono::steady_clock;
    auto next_tick = clock::now() + std::chrono::milliseconds(tick_ms);
    bool pending = false;  // something changed, waiting for the burst to end
    auto last_change = clock::now();

    while (!stop_) {
        const auto now = clock::now();
        auto wake = next_tick;
        if (pending) {
            wake = std::min(wake, last_change + std::chrono::milliseconds(quiet_ms));
        }
        const int wait = static_cast<int>(std::max<long long>(
            0, std::chrono::duration_cast<std::chrono::milliseconds>(wake - now).count()));

        pollfd fds[2] = {{wake_fd_[0], POLLIN, 0}, {inotify_fd_, POLLIN, 0}};
        const nfds_t count = inotify_fd_ >= 0 ? 2 : 1;
        const int ready = ::poll(wake_fd_[0] >= 0 ? fds : fds + 1, wake_fd_[0] >= 0 ? count : count - 1, wait);
        if (stop_) {
            break;
        }
        if (ready > 0 && inotify_fd_ >= 0 && (fds[1].revents & POLLIN) != 0) {
#if defined(__linux__)
            // which files changed doesn't matter here: everything shown is
            // read again. The queue is emptied, and a watch the kernel gave up
            // (its folder was deleted) is forgotten, so the folder is watched
            // again if it comes back.
            alignas(inotify_event) char buffer[8192];
            ssize_t got = 0;
            while ((got = ::read(inotify_fd_, buffer, sizeof buffer)) > 0) {
                for (ssize_t at = 0; at + static_cast<ssize_t>(sizeof(inotify_event)) <= got;) {
                    const inotify_event* event = reinterpret_cast<const inotify_event*>(buffer + at);
                    if ((event->mask & IN_IGNORED) != 0) {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        for (auto it = watched_.begin(); it != watched_.end(); ++it) {
                            if (it->second == event->wd) {
                                watched_.erase(it);
                                break;
                            }
                        }
                    }
                    at += static_cast<ssize_t>(sizeof(inotify_event)) + event->len;
                }
            }
#endif
            pending = true;
            last_change = clock::now();
        }

        const auto after = clock::now();
        if (pending && after - last_change >= std::chrono::milliseconds(quiet_ms)) {
            pending = false;
            if (on_change_) {
                on_change_();
            }
        }
        if (after >= next_tick) {
            next_tick = after + std::chrono::milliseconds(tick_ms);
            if (on_tick_) {
                on_tick_();
            }
        }
    }
}
