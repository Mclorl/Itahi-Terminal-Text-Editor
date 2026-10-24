#pragma once

#include <cstddef>

#include <tree_sitter/api.h>

// one bundled tree-sitter language. The table itself is generated at configure
// time by cmake/syntax_languages.cmake (build/generated/syntax_languages.cpp).
struct SyntaxLanguage {
    const char* name;                    // display name, eg. "C++"
    const TSLanguage* (*language)(void); // tree-sitter grammar entry point
    const char* match;                   // " ext1 ext2 FileName " (space padded)
    const char* highlight_query;         // concatenated highlights.scm files
};

extern const SyntaxLanguage syntax_languages[];
extern const std::size_t syntax_language_count;
