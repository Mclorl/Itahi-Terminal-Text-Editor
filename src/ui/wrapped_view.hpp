#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <ftxui/dom/elements.hpp>

// the editor's text drawn with word wrap: lines longer than `width` cells
// continue on the next row, broken after a space when there is one. Includes
// the line-number gutter (numbers on the first row of each line) and the
// cursor (`underline_active`: its line number underlined, `underline_line`: its
// whole line underlined). The editing itself is still done by the hidden ftxui::Input.
ftxui::Element wrapped_editor_view(const std::string& text, const std::string& path, int cursor, int width,
                                   bool show_numbers, bool underline_active, bool underline_line);

// the byte of the text under the screen position (x, y) of the last drawn
// wrapped view, for mouse clicks; nothing when (x, y) isn't on the text.
std::optional<std::size_t> wrapped_byte_at(int x, int y);

// the width the gutter takes for `text` (0 when line numbers are off).
int wrapped_gutter_width(const std::string& text, bool show_numbers);
