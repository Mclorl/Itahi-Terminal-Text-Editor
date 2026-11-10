#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <ftxui/screen/color.hpp>
#include <tree_sitter/api.h>

#include "core/text_edit.hpp"

// tree-sitter highlighting: which syntax group every byte on screen belongs
// to. No components here; ui/editor_view.cpp draws the result.

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

// files larger than this are shown without colors: the syntax tree of a file
// takes many times its size in memory, and the first parse takes seconds.
inline constexpr std::size_t max_highlight_bytes = 5 * 1024 * 1024;

// syntax tree + colors of one open file. The tree is updated from the edits
// themselves, and the query only runs over the parts of the file that are on
// screen, so typing in a large file stays fast. No copy of the text is kept.
struct HighlightCache {
    // a part of the text that has colors: the SyntaxGroup of every byte of
    // [begin, end). One per window showing the file is enough.
    struct Colored {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::vector<unsigned char> groups;
        std::uint64_t last_used = 0;

        unsigned char group_at(std::size_t byte) const {
            return byte >= begin && byte < end ? groups[byte - begin] : static_cast<unsigned char>(0);
        }
    };

    std::string path;
    std::uint64_t document_id = 0;       // the Document the tree belongs to
    std::uint64_t version = UINT64_MAX;  // of that document's text
    int language_index = -1;             // < 0: no grammar for this file
    bool too_large = false;              // over max_highlight_bytes: no colors
    TSTree* tree = nullptr;              // nullptr: plain text
    std::vector<Colored> colored;
    std::uint64_t uses = 0;

    HighlightCache() = default;
    HighlightCache(const HighlightCache&) = delete;
    HighlightCache& operator=(const HighlightCache&) = delete;
    ~HighlightCache();
};

// asks a document for every edit made since `version` (Document::edits_since).
using EditsSince = std::function<bool(std::uint64_t version, std::vector<TextEdit>& out)>;

// the highlight state for `text`: version `version` of the document
// `document_id`, the file at `path`. A tree parsed for an older version is
// brought up to date with the edits `edits_since` reports, so only what
// changed is parsed again; without them (or when they are unknown) the text
// is parsed from the start. One cache is kept per file, for the last few
// files shown. cache.tree is nullptr when there is nothing to color with.
HighlightCache& highlights_for(const std::string& text, const std::string& path, std::uint64_t document_id,
                               std::uint64_t version, const EditsSince& edits_since = {});

// the colors of [begin, end) of `text` (and a margin around it, so small
// scrolls don't run the query again). Only for a cache with a tree; the
// reference is good until the next call for the same cache.
const HighlightCache::Colored& colored_part(HighlightCache& cache, const std::string& text, std::size_t begin, std::size_t end);

// forget the cache of `path` (its file was closed).
void forget_highlights(const std::string& path);

// name of the language used for `path` ("" when no grammar matches).
std::string syntax_language_name(const std::string& path);
