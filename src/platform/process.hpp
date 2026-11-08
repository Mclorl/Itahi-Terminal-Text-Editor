#pragma once

#include <string>
#include <vector>

// running another program (git, wl-copy, xclip, ...) without a shell.

struct ProcessResult {
    bool started = false;  // false: the program isn't installed (or couldn't start)
    int exit_code = -1;    // -1 when it didn't finish in time
    std::string output;    // what it wrote to stdout
};

// runs `argv` (argv[0] is looked up in PATH), writes `input` to its stdin and,
// with `capture_output`, collects its stdout (otherwise it goes to
// /dev/null: programs such as wl-copy leave a helper running in the
// background that would keep the pipe open). Its stderr is thrown away.
// Gives up, killing it, after `timeout_ms`.
ProcessResult run_process(const std::vector<std::string>& argv, const std::string& input = "",
                          bool capture_output = true, int timeout_ms = 2000);

// true when `program` is found in PATH.
bool program_exists(const std::string& program);
