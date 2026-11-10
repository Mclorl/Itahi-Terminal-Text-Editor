#pragma once

#include <functional>

#include <ftxui/component/component.hpp>

class EditorView;

// the pages of the menu tabs (File, Edit, View, Settings, Help). Every row
// has a margin around it, the row with the focus a bar in front; groups of
// rows have a title; a page scrolls when it doesn't fit.

// the "<-" button that closes the menu.
ftxui::Component file_back_button_func();

ftxui::Component make_file_page();
// `editor` gives the text area of the active window (cut, copy, paste, select all).
ftxui::Component make_edit_page(std::function<EditorView&()> editor);
ftxui::Component make_view_page();
ftxui::Component make_settings_page();
ftxui::Component make_help_page();
