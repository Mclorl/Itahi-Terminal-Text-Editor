#pragma once

#include <functional>
#include <string>
#include <vector>

// what the editor shares besides the open files (editor/workspace.hpp) and
// the settings (editor/settings.hpp): the mode, the command line, and the
// hooks the UI installs so the editor can ask it for things. No FTXUI here.

// command mode: the text of the command line (always starts with the '>'
// prompt), its cursor, the result of the last command shown in its place, and
// the error of a rejected command, shown above the command line.
extern std::string command_line;
extern int command_cursor_index;
extern std::string command_output;
extern std::string command_error;
// the names Tab could complete to, shown under the command line until the
// next key.
extern std::vector<std::string> command_completions;
// the commands run so far, the oldest first: Up / Down in the command line
// bring them back.
extern std::vector<std::string> command_history;
void remember_command(const std::string& command);

// the modes that are available: Normal | Edit | Command. One value, so the
// modes can never disagree.
enum class Mode { Normal, Edit, Command };
extern Mode mode;

// what the editor asks the UI for (installed by ui/app_view.cpp). Any of them
// may be empty: there is no UI in the unit tests.
struct Hooks {
    std::function<void()> redraw;            // draw a new frame
    std::function<void()> quit;              // leave the main loop
    std::function<void()> show_editor;       // close any menu, focus the editor
    std::function<void()> show_help;         // open the Help tab (>help, F1)
    std::function<void()> refresh_explorer;  // re-read the folders shown in the explorer
    std::function<void()> refresh_git;       // ask Git again what changed (after a save, ...)
    // the folder new files go to: the one selected in the explorer (or the
    // folder of the selected file), else the open file's folder, else the
    // opened folder.
    std::function<std::string()> active_folder;
    // what a command without a path acts on (>rename, >rm): the row selected
    // in the explorer while it has the focus, else "" (then: the open file).
    std::function<std::string()> command_target;
};
extern Hooks hooks;

// calls a hook the UI may not have installed.
inline void call(const std::function<void()>& hook) {
    if (hook) {
        hook();
    }
}
