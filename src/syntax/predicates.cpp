#include "syntax/predicates.hpp"

#include <algorithm>
#include <cctype>

static std::string query_string(const TSQuery* query, uint32_t id) {
    uint32_t length = 0;
    const char* text = ts_query_string_value_for_id(query, id, &length);
    return std::string(text, length);
}

// Lua pattern (neovim's #lua-match?) -> ECMAScript regex. Covers what the
// queries use: %a %d %l %u %s %w %x %p classes, %-escapes, sets and the
// lazy `-` repetition.
std::string lua_pattern_to_regex(std::string_view pattern) {
    std::string out;
    bool in_set = false;

    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];

        if (c == '%' && i + 1 < pattern.size()) {
            const char k = pattern[++i];
            const char* cls = nullptr;
            switch (std::tolower(static_cast<unsigned char>(k))) {
                case 'a': cls = "A-Za-z"; break;
                case 'd': cls = "0-9"; break;
                case 'l': cls = "a-z"; break;
                case 'u': cls = "A-Z"; break;
                case 's': cls = " \\t\\n\\r\\f\\v"; break;
                case 'w': cls = "A-Za-z0-9"; break;
                case 'x': cls = "0-9A-Fa-f"; break;
                case 'p': cls = "!-/:-@\\[-`{-~"; break;
                default: break;
            }
            if (cls && in_set) {
                out += cls;
            } else if (cls) {
                out += std::isupper(static_cast<unsigned char>(k)) ? "[^" : "[";
                out += cls;
                out += "]";
            } else {
                out += '\\';
                out += k; // %. %% %- ... = the literal character
            }
            continue;
        }

        if (in_set) {
            if (c == ']') in_set = false;
            if (c == '\\') out += '\\';
            out += c;
            continue;
        }

        if (c == '[') {
            in_set = true;
            out += c;
            if (i + 1 < pattern.size() && pattern[i + 1] == '^') {
                out += '^';
                ++i;
            }
        } else if (c == '-') {
            out += "*?";
        } else if (c == '\\' || c == '{' || c == '}' || c == '|') {
            out += '\\';
            out += c; // special in regex, literal in Lua
        } else {
            out += c;
        }
    }
    return out;
}

static void add_predicate(const TSQuery* query, std::string op, const TSQueryPredicateStep* args, uint32_t arg_count, std::vector<Predicate>& out) {
    Predicate p;

    if (op.empty() || op.back() != '?') {
        return; // directives (#set!, #offset!, ...) don't filter anything.
    }
    if (op.rfind("not-", 0) == 0) {
        p.negated = true;
        op.erase(0, 4);
    }

    bool lua = false;
    if (op == "eq?") {
        p.kind = PredicateKind::Eq;
    } else if (op == "match?" || op == "vim-match?") {
        p.kind = PredicateKind::Match;
    } else if (op == "lua-match?") {
        p.kind = PredicateKind::Match;
        lua = true;
    } else if (op == "any-of?") {
        p.kind = PredicateKind::AnyOf;
    } else if (op == "has-ancestor?") {
        p.kind = PredicateKind::HasAncestor;
    } else if (op == "has-parent?") {
        p.kind = PredicateKind::HasParent;
    } else if (op == "is-not?") {
        return; // (#is-not? local): locals aren't tracked, treat as "not local".
    } else {
        // a predicate we can't evaluate: skip the pattern rather than
        // apply it where it doesn't belong.
        p.kind = PredicateKind::AlwaysFail;
        p.negated = false;
        out.push_back(std::move(p));
        return;
    }

    if (arg_count < 2 || args[0].type != TSQueryPredicateStepTypeCapture) {
        return;
    }
    p.capture_id = args[0].value_id;

    if (p.kind == PredicateKind::Eq && args[1].type == TSQueryPredicateStepTypeCapture) {
        p.compare_with_capture = true;
        p.other_capture_id = args[1].value_id;
    } else if (p.kind == PredicateKind::Eq) {
        p.value = query_string(query, args[1].value_id);
    } else if (p.kind == PredicateKind::Match) {
        // the queries are written for Rust's regex engine; ECMAScript covers
        // the patterns they use. A pattern std::regex rejects makes the
        // predicate fail, so the highlight is skipped instead of wrong.
        std::string pattern = query_string(query, args[1].value_id);
        if (lua) {
            pattern = lua_pattern_to_regex(pattern);
        }
        try {
            p.regex = std::make_shared<std::regex>(pattern, std::regex::ECMAScript | std::regex::optimize);
        } catch (const std::regex_error&) {
            p.regex = nullptr;
        }
    } else {
        for (uint32_t i = 1; i < arg_count; ++i) {
            p.values.push_back(query_string(query, args[i].value_id));
        }
    }

    out.push_back(std::move(p));
}

std::vector<Predicate> read_predicates(const TSQuery* query, uint32_t pattern_index) {
    uint32_t step_count = 0;
    const TSQueryPredicateStep* steps = ts_query_predicates_for_pattern(query, pattern_index, &step_count);

    std::vector<Predicate> predicates;
    uint32_t i = 0;
    while (i < step_count) {
        // one predicate = an operator string + arguments, ended by a Done step.
        uint32_t end = i;
        while (end < step_count && steps[end].type != TSQueryPredicateStepTypeDone) {
            end++;
        }
        if (end > i && steps[i].type == TSQueryPredicateStepTypeString) {
            add_predicate(query, query_string(query, steps[i].value_id), steps + i + 1, end - i - 1, predicates);
        }
        i = end + 1;
    }
    return predicates;
}

// first node captured as `capture_id` in this match.
static bool capture_node(const TSQueryMatch& match, uint32_t capture_id, TSNode& out) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
        if (match.captures[i].index == capture_id) {
            out = match.captures[i].node;
            return true;
        }
    }
    return false;
}

static std::string_view node_text(TSNode node, const std::string& source) {
    const uint32_t start = std::min<uint32_t>(ts_node_start_byte(node), source.size());
    const uint32_t end = std::min<uint32_t>(ts_node_end_byte(node), source.size());
    return std::string_view(source).substr(start, end - start);
}

static bool type_in(TSNode node, const std::vector<std::string>& types) {
    return std::find(types.begin(), types.end(), ts_node_type(node)) != types.end();
}

bool predicates_pass(const std::vector<Predicate>& predicates, const TSQueryMatch& match, const std::string& source) {
    for (const Predicate& p : predicates) {
        if (p.kind == PredicateKind::AlwaysFail) {
            return false;
        }

        TSNode node;
        if (!capture_node(match, p.capture_id, node)) {
            continue; // optional capture not present: nothing to check.
        }
        const std::string_view text = node_text(node, source);

        bool result = false;
        if (p.kind == PredicateKind::Eq) {
            TSNode other;
            result = p.compare_with_capture ? capture_node(match, p.other_capture_id, other) && node_text(other, source) == text
                                            : text == p.value;
        } else if (p.kind == PredicateKind::Match) {
            result = p.regex && std::regex_search(text.begin(), text.end(), *p.regex);
        } else if (p.kind == PredicateKind::AnyOf) {
            result = std::find(p.values.begin(), p.values.end(), text) != p.values.end();
        } else if (p.kind == PredicateKind::HasParent) {
            const TSNode parent = ts_node_parent(node);
            result = !ts_node_is_null(parent) && type_in(parent, p.values);
        } else { // HasAncestor
            for (TSNode up = ts_node_parent(node); !ts_node_is_null(up); up = ts_node_parent(up)) {
                if (type_in(up, p.values)) {
                    result = true;
                    break;
                }
            }
        }

        if (result == p.negated) {
            return false;
        }
    }
    return true;
}
