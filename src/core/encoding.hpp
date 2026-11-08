#pragma once

#include <string>

// true when the data can't be shown safely in the editor: it has bytes that
// only binary files contain (NUL, terminal control codes) or it isn't valid
// UTF-8 (unsupported text encoding).
bool is_binary_or_unsupported_text(const std::string& data);
