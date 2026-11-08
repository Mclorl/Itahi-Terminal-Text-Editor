#include "ui/ui_state.hpp"

ftxui::ScreenInteractive* screen = nullptr;

int status = 1;
int tab_selected = 0;

std::string left_panel_selected_file_path;
std::map<std::string, ftxui::Box> left_panel_row_boxes;

std::string explorer_selected_path;
bool explorer_selected_is_folder = false;

GitStatus git_status;
