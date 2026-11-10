#pragma once

#include <cstddef>

// one change of a text, the way tree-sitter wants to hear about it: which
// bytes were replaced, and where they were as (row, column).

struct TextPoint {
    std::size_t row = 0;     // line, from 0
    std::size_t column = 0;  // bytes from the start of the line
};

struct TextEdit {
    std::size_t start = 0;    // first byte changed
    std::size_t old_end = 0;  // end of the replaced part, before the change
    std::size_t new_end = 0;  // end of what replaced it, after the change
    TextPoint start_point;
    TextPoint old_end_point;
    TextPoint new_end_point;
};
