#include "platform/open_url.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <thread>

extern char** environ;

bool open_url(const std::string& url) {
#if defined(__APPLE__)
    const char* opener = "open";
#else
    const char* opener = "xdg-open";
#endif
    // spawn it with its output sent to /dev/null, so nothing it prints lands
    // on the editor's screen, and don't wait for it.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid = 0;
    char* argv[] = {const_cast<char*>(opener), const_cast<char*>(url.c_str()), nullptr};
    const int result = posix_spawnp(&pid, opener, &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
        return false;
    }

    // reap it when it ends so it doesn't stay a zombie. (Not by ignoring
    // SIGCHLD: that would also hide the exit codes of git and the clipboard
    // programs, see platform/process.hpp.)
    std::thread([pid] {
        int status = 0;
        waitpid(pid, &status, 0);
    }).detach();
    return true;
}
