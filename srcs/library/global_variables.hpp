#pragma once

#include <string>
#include <unordered_set>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

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

// bottom event response. Will be change later for command input.
extern std::string current_events;

extern std::string mouse_sensitivity;

// app settings. Interactive Toggles default

// -- edit -- 
// bool word_wrap = true;

// -- files -- 

// -- view -- 
extern bool auto_save;
extern bool show_line_numbers;
extern bool underline_active_row_on_numbers;
// bool show_status_bar = true;

// global content
extern std::string content;

// Global Components

extern std::unordered_map<std::string, bool> content_already_read;
extern std::unordered_map<std::string, bool> folder_opened_status;

extern std::unordered_map<std::string, bool> file_unsaved_status;

extern std::unordered_map<std::string, std::string> content_read_first;
extern std::unordered_map<std::string, std::string> content_user_updated;


