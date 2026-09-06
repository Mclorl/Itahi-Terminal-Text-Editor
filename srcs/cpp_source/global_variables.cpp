#include "../library/global_variables.hpp"
#include "../library/custom_styling.hpp"

#include <string>
#include <unordered_set>

// status
int status = 1;
int tab_selected = 0;      // 1: File, 2: Edit, 3: View, 4: *Terminal, 5: Help, 0 not active
std::string active_file = "";
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
std::string content;

// Global Components
ftxui::Component file_content_input(std::string &content, int &cursor_index) {
    return ftxui::Input(&content, "", style_file_content_input_option(cursor_index));
}
