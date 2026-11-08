#pragma once

#include <ftxui/component/component.hpp>

// the rows of the menu tabs (File, Edit, View, Settings, Help).

// --- File Back Option ---
ftxui::Component file_back_button_func();

// --- File Menu Choices ---
ftxui::Component file_btn_new_file_func();
ftxui::Component file_btn_save_file_func();

// --- Edit Menu Choices ---
ftxui::Component edit_btn_undo_func();
ftxui::Component edit_btn_redo_func();

// --- View Menu Choices ---
ftxui::Component toggle_linenums_func();
ftxui::Component toggle_underline_active_row_on_numbers_func();
ftxui::Component toggle_underline_active_row_func();
ftxui::Component toggle_syntax_highlighting_func();
ftxui::Component toggle_word_wrap_func();
// one color picker per syntax group.
ftxui::Component syntax_color_settings_func();

// --- Settings Menu Choices ---
ftxui::Component mouse_sens_row_func();
ftxui::Component tab_size_row_func();

// --- Help Menu Choices ---
ftxui::Component btn_about_func();
ftxui::Component btn_ftxui_about_func();
ftxui::Component warning_text_link_direct_func();
