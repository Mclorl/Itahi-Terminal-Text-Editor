#pragma once

#include <filesystem>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;



void opened_directory_file_entry(ftxui::Component container, std::vector<ftxui::Component> &array, std::function<ftxui::ButtonOption(void)> styling, const std::vector<std::string>& file_paths, std::string &content_main_user_updated, std::string &active_file, std::string &long_active_file_name_local, int &left_panel_width, int total_width_size, std::unordered_map<std::string, bool> &content_already_read_local, std::string file_folder_path_key_access = ".");

// NOT USING ANYMORE
// int parse_return_total_int(std::string content);

void opened_directory_entry(ftxui::ScreenInteractive *screen, ftxui::Component &container, std::vector<ftxui::Component> &array, std::function<ftxui::ButtonOption(const std::string&, const std::unordered_set<std::string>&)> styling, std::function<ftxui::ButtonOption(void)> file_styling, const std::vector<std::string>& file_paths, std::unordered_set<std::string>& opened_folders, std::string &content, std::string &active_file, std::string &long_active_file_name_local, ftxui::Component parent_container, int &left_panel_width, int total_width_size);