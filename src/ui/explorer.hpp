#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>

// the file explorer on the left: a row for every folder (opened and closed by
// a click, Enter or Right / Left) and file (opened in the active window),
// unsaved files marked with '*', and Git's state of every file and folder on
// the right. The arrows never leave it: Up on the first row stays there.

// the visible name for the explorer: cut to `available_width` cells with "..."
// at the end, never inside a character.
std::string truncate_name(const std::string& name, int available_width);

// the explorer panel for the folder `root`: its rows, the border and title,
// its own scrolling (the wheel moves the view, not the selection). `width`
// is the panel's width, changed by dragging the separator.
struct ExplorerPanel {
    ftxui::Component rows;    // the rows (focusable: the keyboard moves through them)
    ftxui::Component panel;   // what is drawn: the rows in a bordered box
    std::function<void()> refresh;           // list the folders again from disk
    std::function<bool(int, int)> contains;  // whether a screen position is over the panel
    // the row the user selected last, "" before any was; and whether it is a folder.
    std::function<std::string()> selected_path;
    std::function<bool()> selected_is_folder;
    // the folders whose entries are shown (the opened folder and the open ones in it).
    std::function<std::vector<std::string>()> shown_folders;
};
ExplorerPanel make_explorer_panel(const std::filesystem::path& root, int& width);
