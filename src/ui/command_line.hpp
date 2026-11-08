#pragma once

#include <functional>
#include <string>

#include <ftxui/component/component.hpp>

// command mode line. `on_leave` gives the focus back once the command line
// closes (Escape, Enter, or the '>' prompt deleted with Backspace).
ftxui::Component command_line_input(std::function<void()> on_leave);

// opens the command line with `text` already typed after the '>' prompt.
// Set by main(), used by menu entries such as New File and by / and ?.
extern std::function<void(const std::string&)> open_command_line;
