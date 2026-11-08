#pragma once

#include <map>
#include <string>

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/box.hpp>

#include "platform/git.hpp"

// state that only the UI needs.

extern ftxui::ScreenInteractive* screen;

// 1: the editor is shown, 2: a menu tab is shown.
extern int status;
// 0: no menu tab, 1: File, 2: Edit, 3: View, 4: Settings, 5: Help.
extern int tab_selected;

// left panel (explorer) selection tracking.
// `left_panel_selected_file_path` is the file that currently owns the
// selection; `left_panel_row_boxes` maps every file row to its last rendered
// screen box so the view can be scrolled independently of the selection.
extern std::string left_panel_selected_file_path;
extern std::map<std::string, ftxui::Box> left_panel_row_boxes;

// the last row selected in the explorer, file or folder (keyboard or mouse).
// New File creates its file in this folder, or in the folder of this file.
extern std::string explorer_selected_path;
extern bool explorer_selected_is_folder;

// what Git said about the opened folder last time it was asked (the
// explorer's marks and the status bar's branch). Updated in the background.
extern GitStatus git_status;
