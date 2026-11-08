#include "editor/state.hpp"

// command mode line
std::string command_line;
int command_cursor_index = 0;
std::string command_output;
std::string command_error;
std::vector<std::string> command_completions;

Mode mode = Mode::Normal;

std::function<void()> request_redraw;
std::function<void()> request_quit;
std::function<void()> show_editor;
std::function<void()> request_explorer_refresh;
std::function<void()> request_git_refresh;
std::function<std::string()> active_folder;
