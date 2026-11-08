#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "editor/commands.hpp"

// search in the open file, like vscode's find: while the pattern is typed
// every exact match is highlighted, Enter goes to the next match (and again
// to the one after), Escape ends it. Matching is exact text, case-sensitive.

struct SearchState {
    bool active = false;   // highlights are shown
    std::string pattern;
    bool backward = false; // started with '?'
    std::vector<std::pair<std::size_t, std::size_t>> matches;  // [begin, end) bytes, in order
    int current = -1;      // index of the match the cursor is on, -1 before the first Enter
};

const SearchState& search_state();

// live preview while typing: find and highlight every match of `pattern`.
// The cursor doesn't move.
void search_preview(const std::string& pattern, bool backward);

// Enter (and Down / Up): go to the next match after the cursor, or the
// previous one when `reverse` (the other way for a '?' search). Wraps around.
CommandResult search_step(bool reverse = false);

// Escape: no more highlights. The pattern is kept for n / N.
void search_close();

// 0: `byte` isn't in a highlighted match, 1: in a match, 2: in the current one.
int search_match_kind(std::size_t byte);

// ">/text" / ">?text" typed and run at once (and from tests): set the pattern
// and go to the next match. An empty pattern repeats the last search.
CommandResult search(const std::string& pattern, bool backward);

// n / N: the last search again, in its direction or the other one.
CommandResult search_again(bool reverse);
