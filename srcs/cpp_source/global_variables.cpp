#include "../library/global_variables.hpp"
#include "../library/custom_styling.hpp"

#include <string>
#include <unordered_set>

// global screen
ftxui::ScreenInteractive* screen = nullptr;

// status
int status = 1;
int tab_selected = 0;      // 1: File, 2: Edit, 3: View, 4: *Terminal, 5: Help, 0 not active
std::string active_file = "";

std::string active_file_not_saved;
std::string active_file_saved;

std::string long_active_file_name = "";

std::unordered_set<std::string> opened_folders;

std::string current_events = "System Ready. Select a menu above.";

// mouse sensitivity
std::string mouse_sensitivity = "3";

// app settings. Interactive Toggles default

// -- edit -- 
// bool word_wrap = true;

// -- files -- 

// -- view -- 
bool auto_save = false;
bool show_line_numbers = true;
bool underline_active_row_on_numbers = false;
// bool show_status_bar = true;

// data

// global content
std::string content = "";

// Global Components


std::unordered_map<std::string, bool> content_already_read;
std::unordered_map<std::string, bool> folder_opened_status;

std::unordered_map<std::string, bool> file_unsaved_status;

std::unordered_map<std::string, std::string> content_read_first;
std::unordered_map<std::string, std::string> content_user_updated;

