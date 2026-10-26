#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "ftxui/screen/color.hpp"

#include <functional>
#include <string>

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
ftxui::Component toggle_syntax_highlighting_func();

// --- Terminal Menu Choices ---
// extern ftxui::Component btn_clear_term;
// extern ftxui::Component btn_build_project;

// --- Settings Menu Choices ---
ftxui::Component mouse_sens_row_func();
ftxui::Component tab_size_row_func();

// --- Help Menu Choices ---
ftxui::Component btn_about_func();
ftxui::Component btn_ftxui_about_func();
ftxui::Component warning_text_link_direct_func();

// mouse sensitivity input fallback function.
int get_mouse_sensitivity();

// tab size input fallback function.
int get_tab_size();

ftxui::Component file_content_input(std::string &content, int &cursor_index);

// command mode line. `on_leave` gives the focus back once the command line
// closes (Escape, Enter, or the '>' prompt deleted with Backspace).
ftxui::Component command_line_input(std::function<void()> on_leave);

// result of a command: `ok` is false when the command is invalid or was
// refused, `message` is what to show (an error when not ok).
struct CommandResult {
    bool ok;
    std::string message;
};

// saves the open file (used by ">w" and the Save File button).
CommandResult save_active_file();

// parses and runs a command typed after the '>' prompt.
CommandResult run_command(std::string command);

bool check_content_directory_for_unsaved_file(std::string path);