#pragma once

#include <string>
#include <unordered_set>
#include <map>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

// global screen
extern ftxui::ScreenInteractive* screen;

// FIXED VALUES
#define MAX_WIDTH_INPUT_INT_DEFAULT 4

#define MAX_DIGITS_LENGTH_DEFAULT 2

extern int status;
extern int tab_selected;

extern std::string active_file;

extern std::string active_file_not_saved;
extern std::string active_file_saved;

extern std::string long_active_file_name;

extern std::unordered_set<std::string> opened_folders;

// left panel (explorer) selection tracking.
// `left_panel_selected_file_path` is the file that currently owns the
// selection; `left_panel_row_boxes` maps every file row to its last rendered
// screen box so the view can be scrolled independently of the selection.
extern std::string left_panel_selected_file_path;
extern std::map<std::string, ftxui::Box> left_panel_row_boxes;

// bottom event response. Will be change later for command input.
extern std::string current_events;

// command mode: the text of the command line (always starts with the '>'
// prompt), its cursor, the result of the last command shown in its place, and
// the error of a rejected command, shown above the command line.
extern std::string command_line;
extern int command_cursor_index;
extern std::string command_output;
extern std::string command_error;

extern std::string mouse_sensitivity;
extern std::string tab_size;

// app settings. Interactive Toggles default

// -- edit -- 
// bool word_wrap = true;

// -- files -- 

// -- view -- 
extern bool auto_save;
extern bool show_line_numbers;
extern bool underline_active_row_on_numbers;
extern bool syntax_highlighting;
// bool show_status_bar = true;

// global content
extern std::string content;

// Global Components

extern std::unordered_map<std::string, bool> content_already_read;
extern std::unordered_map<std::string, bool> folder_opened_status;

extern std::unordered_map<std::string, bool> file_truncate_name_bool_status;

extern std::unordered_map<std::string, bool> file_unsaved_status;
extern std::unordered_map<std::string, bool> has_unsaved_file_inside_directory;

extern std::unordered_map<std::string, std::string> content_read_first;
extern std::unordered_map<std::string, std::string> content_user_updated;

// binary / unsupported encoding files the user chose to "Open Anyway".
extern std::unordered_set<std::string> files_opened_anyway;

// global bool modes
extern bool is_normal_mode;
extern bool is_edit_mode;
extern bool is_command_mode;