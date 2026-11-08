#include "platform/process.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace {

    void close_fd(int& fd) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    int milliseconds_until(std::chrono::steady_clock::time_point deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        return static_cast<int>(std::max<long long>(0, left.count()));
    }

}  // namespace

bool program_exists(const std::string& program) {
    const char* path = std::getenv("PATH");
    if (!path || program.empty()) {
        return false;
    }
    const std::string folders = path;
    std::size_t start = 0;
    while (start <= folders.size()) {
        std::size_t end = folders.find(':', start);
        if (end == std::string::npos) {
            end = folders.size();
        }
        const std::string folder = folders.substr(start, end - start);
        if (::access(((folder.empty() ? "." : folder) + "/" + program).c_str(), X_OK) == 0) {
            return true;
        }
        start = end + 1;
    }
    return false;
}

ProcessResult run_process(const std::vector<std::string>& argv, const std::string& input, bool capture_output,
                          int timeout_ms) {
    ProcessResult result;
    if (argv.empty()) {
        return result;
    }
    // a program that exits before reading all its input must not kill the
    // editor with SIGPIPE: the write just fails (EPIPE) instead.
    static const bool sigpipe_ignored = [] { std::signal(SIGPIPE, SIG_IGN); return true; }();
    (void)sigpipe_ignored;

    int to_child[2] = {-1, -1};
    int from_child[2] = {-1, -1};
    if (::pipe2(to_child, O_CLOEXEC) != 0) {
        return result;
    }
    if (capture_output && ::pipe2(from_child, O_CLOEXEC) != 0) {
        close_fd(to_child[0]);
        close_fd(to_child[1]);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, to_child[0], STDIN_FILENO);
    if (capture_output) {
        posix_spawn_file_actions_adddup2(&actions, from_child[1], STDOUT_FILENO);
    } else {
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    }
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    std::vector<std::string> args = argv;
    std::vector<char*> raw;
    for (std::string& arg : args) {
        raw.push_back(arg.data());
    }
    raw.push_back(nullptr);

    pid_t pid = 0;
    const int spawned = posix_spawnp(&pid, raw[0], &actions, nullptr, raw.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close_fd(to_child[0]);
    close_fd(from_child[1]);
    if (spawned != 0) {
        close_fd(to_child[1]);
        close_fd(from_child[0]);
        return result;
    }
    result.started = true;

    // feed stdin and read stdout together, so neither side can block the other.
    ::fcntl(to_child[1], F_SETFL, O_NONBLOCK);
    if (from_child[0] >= 0) {
        ::fcntl(from_child[0], F_SETFL, O_NONBLOCK);
    }
    if (input.empty()) {
        close_fd(to_child[1]);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::size_t written = 0;
    char buffer[65536];
    while ((to_child[1] >= 0 || from_child[0] >= 0) && milliseconds_until(deadline) > 0) {
        pollfd fds[2];
        nfds_t count = 0;
        int out_slot = -1;
        int in_slot = -1;
        if (from_child[0] >= 0) {
            out_slot = static_cast<int>(count);
            fds[count++] = {from_child[0], POLLIN, 0};
        }
        if (to_child[1] >= 0) {
            in_slot = static_cast<int>(count);
            fds[count++] = {to_child[1], POLLOUT, 0};
        }
        if (::poll(fds, count, milliseconds_until(deadline)) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (in_slot >= 0 && fds[in_slot].revents != 0) {
            const ssize_t n = ::write(to_child[1], input.data() + written, input.size() - written);
            if (n > 0) {
                written += static_cast<std::size_t>(n);
            }
            if ((n < 0 && errno != EAGAIN && errno != EINTR) || written == input.size()) {
                close_fd(to_child[1]);  // all written (or the program stopped reading)
            }
        }
        if (out_slot >= 0 && fds[out_slot].revents != 0) {
            const ssize_t n = ::read(from_child[0], buffer, sizeof buffer);
            if (n > 0) {
                result.output.append(buffer, static_cast<std::size_t>(n));
            } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                close_fd(from_child[0]);  // end of its output
            }
        }
    }
    close_fd(to_child[1]);
    close_fd(from_child[0]);

    // wait for it to end, but not forever.
    int status = 0;
    while (true) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            }
            break;
        }
        if (done < 0 && errno != EINTR) {
            break;  // already reaped elsewhere
        }
        if (milliseconds_until(deadline) == 0) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return result;
}
