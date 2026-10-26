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

// true when the data can't be shown safely in the editor: it has bytes that
// only binary files contain (NUL, terminal control codes) or it isn't valid
// UTF-8 (unsupported text encoding).
bool is_binary_or_unsupported_text(const std::string& data);

// true while the file at `path` must stay hidden behind the "Open Anyway"
// warning (binary / unsupported encoding, and not opened anyway yet).
bool is_file_hidden_from_editor(const std::string& path);