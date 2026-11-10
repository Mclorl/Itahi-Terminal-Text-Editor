#pragma once

#include <string>
#include <vector>

class Document;

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

// reads `document` again from disk (">e", ">e!"). Refuses while it has
// unsaved changes, unless `force`; the dropped changes can still be undone.
CommandResult reload_document(Document& document, bool force);

// looks at every open file on disk: one that another program changed is read
// again when it has no unsaved changes (and settings.auto_reload is on), and
// reported otherwise. Returns what to tell the user, "" when nothing changed.
std::string check_files_on_disk();

// parses and runs a command typed after the '>' prompt.
CommandResult run_command(std::string command);

// every command, for the help screen: how it is typed and what it does,
// grouped ("Files", "Windows", ...) in the order they are shown.
struct CommandHelp {
    std::string group;
    std::string usage;
    std::string help;
};
const std::vector<CommandHelp>& command_help();
