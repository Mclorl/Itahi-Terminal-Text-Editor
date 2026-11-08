#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

// how a line of text is laid out in terminal columns. The one place that
// decides it: the editor draws with it, and moves the cursor, maps mouse
// clicks and wraps lines with it, so they can never disagree.

struct GlyphCell {
    std::size_t byte;  // where the glyph starts in the text
    std::size_t size;  // its bytes (a character and the combining marks after it)
    int column;        // its first column, counted from the start of the line
    int width;         // its columns: 1; 2 for full-width characters; up to the
                       // tab size for a tab; 0 for control characters (not drawn)
    bool invalid;      // a byte that isn't valid UTF-8 (drawn as U+FFFD)
};

// the glyphs of text[begin, end), one line without its line break. A tab
// reaches the next multiple of `tab_size` columns.
void layout_line(std::string_view text, std::size_t begin, std::size_t end, int tab_size, std::vector<GlyphCell>& out);

// the line's glyphs split into rows of at most `width` columns, broken after
// the last space of a row when there is one (word wrap). Returns the index of
// the first glyph of every row; an empty line is one row.
std::vector<std::size_t> wrap_rows(const std::vector<GlyphCell>& glyphs, std::string_view text, int width);

// the columns `glyphs` take in all.
int line_width(const std::vector<GlyphCell>& glyphs);
