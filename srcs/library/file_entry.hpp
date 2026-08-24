#pragma once

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

std::string readFile(std::string argument_file);

fs::path get_current_path(std::error_code& ec);

std::string iterate_current_path_shallow(std::string userPath);