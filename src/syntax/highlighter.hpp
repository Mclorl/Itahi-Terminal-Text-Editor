#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <ftxui/screen/color.hpp>
#include <tree_sitter/api.h>

// tree-sitter highlighting: which syntax group every byte of a file belongs
// to. No components here; ui/syntax_paint.hpp draws the result.

// syntax highlighting groups. Every tree-sitter capture name (eg.
// "keyword.return", "string.escape") is folded into one of these, and each
// group has a user-changeable color (View tab).
enum class SyntaxGroup : unsigned char {
    None = 0,
    Keyword,
    Preprocessor,
    Type,
    Function,
    Variable,
    Property,
    String,
    Special,   // escape sequences, regex
    Number,
    Constant,  // true/false/null, constants, this/self
    Comment,
    Operator,
    Punctuation,
    Tag,       // html/jsx tags and attributes, links
    Heading,   // markdown headings
    DiffAdded,
    DiffRemoved,
    Count,
};

// syntax tree + colors of the last rendered content. The tree is updated
// incrementally on edits, and the query only runs over the part of the
// file that is on screen, so typing in a large file stays fast.
struct HighlightCache {
    std::string path;
    std::string content;
    int language_index = -1;
    TSTree* tree = nullptr;
    std::vector<std::size_t> line_starts;    // byte offset of every line
    std::vector<unsigned char> byte_groups;  // SyntaxGroup per byte, valid in [colored_begin, colored_end)
    std::size_t colored_begin = 0;
    std::size_t colored_end = 0;
};


// the highlight state for `content` (the file at `path`), updated
// incrementally from the previous call. language_index < 0: no grammar.
HighlightCache& highlights_for(const std::string& text, const std::string& path);

// make sure byte_groups is valid for [begin, end) (plus a margin).
void ensure_colored(HighlightCache& cache, std::size_t begin, std::size_t end);

// name of the language used for `path` ("" when no grammar matches).
std::string syntax_language_name(const std::string& path);

// the color of every byte of `text` (Color::Default where there is none),
// for views that draw the text themselves (word wrap).
void syntax_colors(const std::string& text, const std::string& path, std::vector<ftxui::Color>& out);
