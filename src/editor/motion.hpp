#pragma once

#include <cstddef>
#include <string_view>

// cursor moves that only need the bytes of the text (the ones that depend on
// how the text is drawn, such as Up / Down, are in ui/editor_view.cpp).

// Ctrl+Right / Ctrl+Left: to the end of the next word, or the start of the
// previous one. Letters, digits, '_' and every non-ASCII character make
// words; a run of punctuation counts as a word too; spaces and line breaks
// are skipped.
std::size_t word_right(std::string_view text, std::size_t from);
std::size_t word_left(std::string_view text, std::size_t from);

// the start of the next / previous character: never inside a UTF-8 sequence.
std::size_t next_char(std::string_view text, std::size_t from);
std::size_t previous_char(std::string_view text, std::size_t from);
