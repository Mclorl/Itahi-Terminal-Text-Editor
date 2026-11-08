#pragma once

#include <functional>
#include <string>

#include <ftxui/component/component.hpp>

// command mode line. `on_leave` gives the focus back once the command line
// closes (Escape, Enter, or the '>' prompt deleted with Backspace).
ftxui::Component command_line_input(std::function<void()> on_leave);

// the bottom line of the main view: while typing a command, the command line
// (`input`) with the error of a rejected command above it, where a new file
// will go, the search's "2 of 5", and Tab's options on a gray strip under it.
// Otherwise the last error (red) or result (dim) until the next key press.
ftxui::Element command_line_area(const ftxui::Component& input);

// the grayed-out hint drawn after the cursor: the active folder while ">e" has
// no file name yet ("" when there is nothing to suggest). Tab accepts it.
std::string command_line_ghost();

// opens the command line with `text` already typed after the '>' prompt.
// Set by main(), used by menu entries such as New File and by / and ?.
extern std::function<void(const std::string&)> open_command_line;
