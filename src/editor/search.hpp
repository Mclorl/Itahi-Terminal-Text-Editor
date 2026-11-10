#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "editor/commands.hpp"

class Document;

// search in the open file, like vscode's find: while the pattern is typed
// every match is highlighted, Enter goes to the next match (and again to the
// one after), Escape ends it. The pattern is exact text. It matches upper and
// lower case alike when "ignore case" is on (Alt+C while searching), or when
// it ends in "\c" like in vim ("\C" for the opposite); with "whole words"
// (Alt+W) a match can't be a part of a longer word. Both options are settings
// (settings.search_ignore_case, settings.search_whole_word).

struct SearchState {
    bool active = false;   // highlights are shown
    std::string pattern;   // as typed, without a "\c" / "\C"
    bool backward = false; // started with '?'
    bool ignore_case = false;
    bool whole_word = false;
    // the document the matches were found in, and its version then.
    const Document* document = nullptr;
    std::uint64_t version = 0;
    std::vector<std::pair<std::size_t, std::size_t>> matches;  // [begin, end) bytes, in order
    int current = -1;      // index of the match the cursor is on, -1 before the first Enter
};

const SearchState& search_state();

// every match of `pattern` in `text`, in order, never overlapping.
std::vector<std::pair<std::size_t, std::size_t>> find_matches(const std::string& text, const std::string& pattern,
                                                              bool ignore_case, bool whole_word);

// live preview while typing: find and highlight every match of `pattern`.
// The cursor doesn't move.
void search_preview(const std::string& pattern, bool backward);

// Enter (and Down / Up): go to the next match after the cursor, or the
// previous one when `reverse` (the other way for a '?' search). Wraps around.
CommandResult search_step(bool reverse = false);

// Escape: no more highlights. The pattern is kept for n / N.
void search_close();

// 0: `byte` of `document` isn't in a highlighted match, 1: in a match, 2: in
// the current one.
int search_match_kind(const Document& document, std::size_t byte);

// ">/text" / ">?text" typed and run at once (and from tests): set the pattern
// and go to the next match. An empty pattern repeats the last search.
CommandResult search(const std::string& pattern, bool backward);

// n / N: the last search again, in its direction or the other one.
CommandResult search_again(bool reverse);
