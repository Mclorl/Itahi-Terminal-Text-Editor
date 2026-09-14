#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "ftxui/screen/color.hpp"

// tab functions

// --- File Back Option ---
ftxui::Component file_back_button_func();

// --- File Menu Choices ---
ftxui::Component file_toggle_autosave_func();
ftxui::Component file_btn_new_file_func();
ftxui::Component file_btn_save_file_func();

// --- Edit Menu Choices ---
ftxui::Component edit_btn_undo_func();
ftxui::Component edit_btn_redo_func();
// extern ftxui::Component edit_toggle_word_wrap;

// --- View Menu Choices ---
ftxui::Component toggle_linenums_func();
ftxui::Component toggle_underline_active_row_on_numbers_func();

// --- Terminal Menu Choices ---
// extern ftxui::Component btn_clear_term;
// extern ftxui::Component btn_build_project;

// --- Settings Menu Choices ---
ftxui::Component mouse_sens_row_func();

// --- Help Menu Choices ---
ftxui::Component btn_about_func();
ftxui::Component btn_ftxui_about_func();
ftxui::Component warning_text_link_direct_func();

// mouse sensitivity input fallback function.
int get_mouse_sensitivity();

ftxui::Component file_content_input(std::string &content, int &cursor_index);

bool check_content_directory_for_unsaved_file(std::string path);