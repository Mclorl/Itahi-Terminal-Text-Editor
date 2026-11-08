#include "editor/search.hpp"

#include <algorithm>

#include "editor/state.hpp"

namespace {

    SearchState state;

    std::vector<std::pair<std::size_t, std::size_t>> find_all(const std::string& text, const std::string& pattern) {
        std::vector<std::pair<std::size_t, std::size_t>> matches;
        if (pattern.empty()) {
            return matches;
        }
        for (std::size_t at = text.find(pattern); at != std::string::npos; at = text.find(pattern, at + pattern.size())) {
            matches.emplace_back(at, at + pattern.size());
        }
        return matches;
    }

    std::string count_message() {
        return std::to_string(state.current + 1) + " of " + std::to_string(state.matches.size());
    }

}  // namespace

const SearchState& search_state() {
    return state;
}

void search_preview(const std::string& pattern, bool backward) {
    state.active = true;
    state.pattern = pattern;
    state.backward = backward;
    state.matches = find_all(content, pattern);
    state.current = -1;
}

CommandResult search_step(bool reverse) {
    if (long_active_file_name.empty()) {
        return {false, "No file is open"};
    }
    if (state.pattern.empty()) {
        return {false, "No previous search"};
    }
    // the text may have changed since the matches were found (n / N later).
    state.matches = find_all(content, state.pattern);
    if (state.matches.empty()) {
        state.current = -1;
        return {false, "Pattern not found: " + state.pattern};
    }

    const bool up = state.backward != reverse;
    const std::size_t at = static_cast<std::size_t>(std::max(cursor_index, 0));
    // the first step goes to the first match at or after the cursor; the
    // next steps move on from the match the cursor is on.
    const bool first_step = state.current < 0;
    std::string wrapped;
    int target = 0;
    const int count = static_cast<int>(state.matches.size());
    if (!up) {
        const auto next = std::find_if(state.matches.begin(), state.matches.end(), [&](const auto& m) {
            return first_step ? m.first >= at : m.first > at;
        });
        if (next == state.matches.end()) {
            target = 0;
            wrapped = " (search hit BOTTOM, continuing at TOP)";
        } else {
            target = static_cast<int>(next - state.matches.begin());
        }
    } else {
        const auto previous = std::find_if(state.matches.rbegin(), state.matches.rend(), [&](const auto& m) {
            return m.first < at;
        });
        if (previous == state.matches.rend()) {
            target = count - 1;
            wrapped = " (search hit TOP, continuing at BOTTOM)";
        } else {
            target = static_cast<int>(state.matches.rend() - previous) - 1;
        }
    }

    state.current = target;
    cursor_index = static_cast<int>(state.matches[static_cast<std::size_t>(target)].first);
    return {true, count_message() + wrapped};
}

void search_close() {
    state.active = false;
    state.current = -1;
}

int search_match_kind(std::size_t byte) {
    if (!state.active || state.matches.empty()) {
        return 0;
    }
    // the last match starting at or before `byte`.
    const auto after = std::upper_bound(state.matches.begin(), state.matches.end(), byte,
                                        [](std::size_t b, const auto& m) { return b < m.first; });
    if (after == state.matches.begin()) {
        return 0;
    }
    const auto match = std::prev(after);
    if (byte >= match->second) {
        return 0;
    }
    return static_cast<int>(match - state.matches.begin()) == state.current ? 2 : 1;
}

CommandResult search(const std::string& pattern, bool backward) {
    if (!pattern.empty()) {
        state.pattern = pattern;
    }
    state.backward = backward;
    state.current = 0;  // move on from the cursor, like n
    const CommandResult result = search_step(false);
    if (result.ok) {
        return {true, (backward ? "?" : "/") + state.pattern + "  " + result.message};
    }
    return result;
}

CommandResult search_again(bool reverse) {
    state.current = 0;
    return search_step(reverse);
}
