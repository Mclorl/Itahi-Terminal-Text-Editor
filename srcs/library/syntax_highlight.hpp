#pragma once

#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

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

// name of the language used for `path` ("" when no grammar matches).
std::string syntax_language_name(const std::string& path);

// decorator for the editor's Input element: once the Input has drawn the text,
// recolor every cell according to the syntax of `content` (the file at `path`).
// The Input itself is untouched, so the cursor, mouse and editing keep working.
ftxui::Decorator syntax_highlight(const std::string& content, const std::string& path);

// View tab rows: one color picker per syntax group.
ftxui::Component syntax_color_settings_func();
