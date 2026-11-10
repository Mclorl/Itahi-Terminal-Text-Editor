#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// notices files and folders changed by other programs (a terminal, git, a
// build), and keeps a steady tick for work done in the background.
//
// On Linux the folders given to watch() are watched with inotify; `on_change`
// is called a moment after something was created, deleted, renamed or written
// in one of them (once for a burst of changes). `on_tick` is called about
// once a second. Both are called on the watcher's own thread: hand the work
// over to the UI thread from there.

class FileWatcher {
public:
    FileWatcher(std::function<void()> on_change, std::function<void()> on_tick);
    ~FileWatcher();
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // the folders to watch from now on (not what is inside their subfolders).
    // Folders that can't be watched are left out.
    void watch(const std::vector<std::string>& folders);

    // true when changes are noticed as they happen (inotify is there).
    bool live() const { return inotify_fd_ >= 0; }

private:
    void run();

    std::function<void()> on_change_;
    std::function<void()> on_tick_;
    int inotify_fd_ = -1;
    int wake_fd_[2] = {-1, -1};  // a pipe: written to stop the thread
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::map<std::string, int> watched_;  // folder -> inotify watch
    std::thread thread_;
};
