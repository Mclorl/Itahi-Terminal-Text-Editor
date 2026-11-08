#pragma once

#include <cstdint>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <tree_sitter/api.h>

// tree-sitter only parses predicates such as (#match? @x "^[A-Z]"); the
// program running the query has to evaluate them. Some grammars use
// neovim's predicates (#lua-match?, #has-ancestor?, ...), handled too.

enum class PredicateKind { Eq, Match, AnyOf, HasAncestor, HasParent, AlwaysFail };

struct Predicate {
    PredicateKind kind = PredicateKind::Eq;
    bool negated = false;                   // not-eq? / not-match? / not-any-of? / ...
    uint32_t capture_id = 0;
    bool compare_with_capture = false;      // (#eq? @a @b)
    uint32_t other_capture_id = 0;
    std::string value;                      // (#eq? @a "text")
    std::vector<std::string> values;        // (#any-of? @a "x" "y"), node types for has-*
    std::shared_ptr<std::regex> regex;      // (#match? @a "re"), null if unsupported
};

// Lua pattern (neovim's #lua-match?) -> ECMAScript regex.
std::string lua_pattern_to_regex(std::string_view pattern);

// the predicates of one pattern of the query.
std::vector<Predicate> read_predicates(const TSQuery* query, uint32_t pattern_index);

// true when every predicate holds for this match.
bool predicates_pass(const std::vector<Predicate>& predicates, const TSQueryMatch& match, const std::string& source);
