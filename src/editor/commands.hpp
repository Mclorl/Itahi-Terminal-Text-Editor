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

// closes the open file's tab (">bd", a click on its x). Refuses while it has
// unsaved changes, unless `force` (">bd!").
CommandResult close_active_file(bool force = false);

// shows the next (+1) or previous (-1) open file (">bn", ">bp", Alt+Right / Alt+Left).
CommandResult switch_file(int step);

// parses and runs a command typed after the '>' prompt.
CommandResult run_command(std::string command);
