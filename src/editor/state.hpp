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

// the modes that are available: Normal | Edit | Command. One value, so the
// modes can never disagree.
enum class Mode { Normal, Edit, Command };
extern Mode mode;

// hooks the UI installs (ui/app_view.cpp). Each may be empty.
extern std::function<void()> request_redraw;            // draw a new frame
extern std::function<void()> request_quit;              // leave the main loop
extern std::function<void()> show_editor;               // close any menu, focus the editor
extern std::function<void()> request_explorer_refresh;  // re-read the folders shown in the explorer
extern std::function<void()> request_git_refresh;       // ask Git again what changed (after a save, ...)
// the folder new files go to: the one selected in the explorer (or the folder
// of the selected file), else the open file's folder, else the opened folder.
extern std::function<std::string()> active_folder;
