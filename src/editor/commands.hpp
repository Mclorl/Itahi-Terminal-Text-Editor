#pragma once

#include <string>

// the commands typed after the '>' prompt, and the actions behind them.

// result of a command: `ok` is false when the command is invalid or was
// refused, `message` is what to show (an error when not ok).
struct CommandResult {
    bool ok;
    std::string message;
};

// saves the open file (">w", Ctrl+S, the Save File button). Refuses when the
// file changed on disk since it was read, unless `force` (">w!").
CommandResult save_active_file(bool force = false);

// parses and runs a command typed after the '>' prompt.
CommandResult run_command(std::string command);
