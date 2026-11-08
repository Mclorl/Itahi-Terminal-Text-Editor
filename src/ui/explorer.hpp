#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

// the visible name for the explorer: cut to `available_width` cells with "..."
// at the end, never inside a character. Records in
// file_truncate_name_bool_status[path] whether it had to be cut.
std::string truncate_name(const std::string& name, int available_width, const std::string& path);

// adds a button for every regular file in `file_paths` to `container`.
void opened_directory_file_entry(ftxui::Component container, const std::vector<std::filesystem::path>& file_paths,
                                 std::string &content_main_user_updated, std::string &active_file_local,
                                 std::string &long_active_file_name_local, int &left_panel_width, int indentation);

// adds a folder row (and its children while open) for every folder in `file_paths` to `parent_container`.
void opened_directory_entry(ftxui::ScreenInteractive *screen_local, const std::vector<std::filesystem::path>& file_paths,
                            std::unordered_set<std::string>& opened_folders_local, std::string &content_local,
                            std::string &active_file_local, std::string &long_active_file_name_local,
                            ftxui::Component parent_container, int &left_panel_width, int indentation);
