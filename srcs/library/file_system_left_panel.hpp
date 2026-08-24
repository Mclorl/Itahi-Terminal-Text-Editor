#pragma once

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;



void opened_directory_file_entry(std::vector<ftxui::Component> &array, std::function<ftxui::ButtonOption(void)> styling, const std::vector<std::string>& file_paths, std::string &content, std::string &active_file) ;

// NOT USING ANYMORE
// int parse_return_total_int(std::string content);

