#include "editor/search.hpp"

#include <algorithm>
#include <cctype>

#include "editor/settings.hpp"
#include "editor/workspace.hpp"

namespace {

    SearchState state;

    char lower(char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    bool is_word_byte(char c) {
        const unsigned char u = static_cast<unsigned char>(c);
        return u >= 0x80 || std::isalnum(u) || u == '_';
    }

    // takes a trailing or leading "\c" / "\C" out of `pattern` and applies it.
    std::string without_case_mark(std::string pattern, bool& ignore_case) {
        for (const char* mark : {"\\c", "\\C"}) {
            const bool ignore = mark[1] == 'c';
            if (pattern.size() >= 2 && pattern.compare(pattern.size() - 2, 2, mark) == 0) {
                pattern.erase(pattern.size() - 2);
                ignore_case = ignore;
            } else if (pattern.size() >= 2 && pattern.compare(0, 2, mark) == 0) {
                pattern.erase(0, 2);
                ignore_case = ignore;
            }
        }
        return pattern;
    }

    void find_in(const Document* document) {
        state.document = document;
        state.version = document ? document->version() : 0;
        state.matches = document ? find_matches(document->text(), state.pattern, state.ignore_case, state.whole_word)
                                 : std::vector<std::pair<std::size_t, std::size_t>>{};
    }

    // the options are settings (Alt+C / Alt+W while searching); a "\c" in
    // the pattern changes the case option for that search only.
    void set_pattern(const std::string& typed) {
        state.ignore_case = settings.search_ignore_case;
        state.whole_word = settings.search_whole_word;
        state.pattern = without_case_mark(typed, state.ignore_case);
    }

    std::string count_message() {
        return std::to_string(state.current + 1) + " of " + std::to_string(state.matches.size());
    }

}  // namespace

const SearchState& search_state() {
    return state;
}

std::vector<std::pair<std::size_t, std::size_t>> find_matches(const std::string& text, const std::string& pattern,
                                                              bool ignore_case, bool whole_word) {
    std::vector<std::pair<std::size_t, std::size_t>> matches;
    if (pattern.empty() || pattern.size() > text.size()) {
        return matches;
    }
    const auto whole = [&](std::size_t at) {
        const std::size_t end = at + pattern.size();
        return (at == 0 || !is_word_byte(text[at - 1])) && (end == text.size() || !is_word_byte(text[end]));
    };

    if (!ignore_case) {
        for (std::size_t at = text.find(pattern); at != std::string::npos;) {
            if (!whole_word || whole(at)) {
                matches.emplace_back(at, at + pattern.size());
                at = text.find(pattern, at + pattern.size());
            } else {
                at = text.find(pattern, at + 1);
            }
        }
        return matches;
    }

    // ASCII letters match either case; the bytes of other characters match exactly.
    std::string needle = pattern;
    std::transform(needle.begin(), needle.end(), needle.begin(), lower);
    const char first = needle[0];
    const char first_upper = static_cast<char>(std::toupper(static_cast<unsigned char>(first)));
    const std::size_t last_start = text.size() - needle.size();
    for (std::size_t at = 0; at <= last_start;) {
        if (text[at] != first && text[at] != first_upper) {
            ++at;
            continue;
        }
        bool same = true;
        for (std::size_t k = 1; same && k < needle.size(); ++k) {
            same = lower(text[at + k]) == needle[k];
        }
        if (same && (!whole_word || whole(at))) {
            matches.emplace_back(at, at + needle.size());
            at += needle.size();
        } else {
            ++at;
        }
    }
    return matches;
}

void search_preview(const std::string& pattern, bool backward) {
    state.active = true;
    state.backward = backward;
    set_pattern(pattern);
    find_in(workspace.active());
    state.current = -1;
}

CommandResult search_step(bool reverse) {
    Document* document = workspace.active();
    if (!document) {
        return {false, "No file is open"};
    }
    if (state.pattern.empty()) {
        return {false, "No previous search"};
    }
    // the text may have changed since the matches were found (n / N later).
    find_in(document);
    if (state.matches.empty()) {
        state.current = -1;
        return {false, "Pattern not found: " + state.pattern};
    }

    const bool up = state.backward != reverse;
    const std::size_t at = document->cursor();
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
    document->set_cursor(state.matches[static_cast<std::size_t>(target)].first);
    return {true, count_message() + wrapped};
}

void search_close() {
    state.active = false;
    state.current = -1;
    // the matches are of no use any more: a search in a large file can have
    // found a great many.
    state.matches.clear();
    state.matches.shrink_to_fit();
    state.document = nullptr;
}

int search_match_kind(const Document& document, std::size_t byte) {
    if (!state.active || state.matches.empty() || state.document != &document || state.version != document.version()) {
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
        set_pattern(pattern);
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
