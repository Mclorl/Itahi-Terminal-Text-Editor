#pragma once

#include <filesystem>
#include <string>

// opens `path` in the editor (a missing path becomes a new, empty file).
// Returns false, with command_error set, when the file can't be read.
bool open_file(const std::filesystem::path& path, std::string& content_main_user_updated, std::string& active_file_local,
               std::string& long_active_file_name_local);
