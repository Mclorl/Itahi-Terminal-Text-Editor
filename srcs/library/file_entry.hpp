#pragma once

#include <filesystem>
#include <system_error>

#include <sstream>

#include "../library/global_variables.hpp"

namespace fs = std::filesystem;

std::string write_file_entry(std::string file_name, std::string &updated_content);

std::string readFile(std::string argument_file);

fs::path get_current_path(std::error_code& ec);

std::string iterate_current_path_shallow(std::string userPath);