#pragma once

#include <ftxui/component/screen_interactive.hpp>

#include "platform/git.hpp"

// state that only the UI needs.

extern ftxui::ScreenInteractive* screen;

// 1: the editor is shown, 2: a menu tab is shown.
extern int status;
// 0: no menu tab, 1: File, 2: Edit, 3: View, 4: Settings, 5: Help.
extern int tab_selected;

// what Git said about the opened folder last time it was asked (the
// explorer's marks and the status bar's branch). Updated in the background.
extern GitStatus git_status;
