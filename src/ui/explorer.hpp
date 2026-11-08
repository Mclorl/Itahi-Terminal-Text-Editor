#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>

// the file explorer on the left: a row for every folder (opened and closed by
// a click or Enter) and file (opened in the editor), unsaved files marked
// with '*', and Git's state of every file and folder on the right.

// the visible name for the explorer: cut to `available_width` cells with "..."
// at the end, never inside a character. Remembers for `path` whether it had
// to be cut.
std::string truncate_name(const std::string& name, int available_width, const std::string& path);

// the explorer panel for the folder `root`: its rows, the border and title,
// its own scrolling (the wheel moves the view, not the selection). `width`
// is the panel's width, changed by dragging the separator.
struct ExplorerPanel {
    ftxui::Component rows;    // the rows (focusable: the keyboard moves through them)
    ftxui::Component panel;   // what is drawn: the rows in a bordered, scrolling box
    std::function<void()> refresh;  // list the folders again from disk
    std::function<bool(int, int)> contains;  // whether a screen position is over the panel
};
ExplorerPanel make_explorer_panel(const std::filesystem::path& root, int& width);
