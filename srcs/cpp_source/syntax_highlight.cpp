#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

#include <tree_sitter/api.h>

#include "../library/syntax_highlight.hpp"
#include "../library/syntax_languages.hpp"
#include "../library/global_variables.hpp"

namespace fs = std::filesystem;

namespace {

    constexpr int group_count = static_cast<int>(SyntaxGroup::Count);

    // --- colors ---

    struct NamedColor {
        const char* name;
        ftxui::Color color;
    };

    // colors the user can pick from in the View tab. Index 0 ("Default") means
    // "leave the cell as it is".
    const std::vector<NamedColor>& color_palette() {
        static const std::vector<NamedColor> palette = {
            {"Default",       ftxui::Color::Default},
            {"White",         ftxui::Color::White},
            {"Light Gray",    ftxui::Color::GrayLight},
            {"Dark Gray",     ftxui::Color::GrayDark},
            {"Red",           ftxui::Color::Red},
            {"Light Red",     ftxui::Color::RedLight},
            {"Green",         ftxui::Color::Green},
            {"Light Green",   ftxui::Color::GreenLight},
            {"Yellow",        ftxui::Color::Yellow},
            {"Light Yellow",  ftxui::Color::YellowLight},
            {"Blue",          ftxui::Color::Blue},
            {"Light Blue",    ftxui::Color::BlueLight},
            {"Magenta",       ftxui::Color::Magenta},
            {"Light Magenta", ftxui::Color::MagentaLight},
            {"Cyan",          ftxui::Color::Cyan},
            {"Light Cyan",    ftxui::Color::CyanLight},
            {"Orange",        ftxui::Color::Orange1},
            {"Gold",          ftxui::Color::Gold1},
            {"Pink",          ftxui::Color::Pink1},
            {"Light Pink",    ftxui::Color::LightPink1},
            {"Salmon",        ftxui::Color::Salmon1},
            {"Violet",        ftxui::Color::Violet},
            {"Purple",        ftxui::Color::Purple},
            {"Sky Blue",      ftxui::Color::SkyBlue1},
            {"Spring Green",  ftxui::Color::SpringGreen1},
            {"Aquamarine",    ftxui::Color::Aquamarine1},
            {"Khaki",         ftxui::Color::Khaki1},
        };
        return palette;
    }

    int palette_index(std::string_view name) {
        const auto& palette = color_palette();
        for (std::size_t i = 0; i < palette.size(); ++i) {
            if (name == palette[i].name) {
                return static_cast<int>(i);
            }
        }
        return 0;
    }

    struct GroupSetting {
        const char* label;
        int color_index; // into color_palette()
    };

    // label + current color of every group. Defaults follow vim's default
    // dark colorscheme (Statement yellow, Type green, Constant pink, ...).
    std::array<GroupSetting, group_count>& group_settings() {
        static std::array<GroupSetting, group_count> settings = [] {
            std::array<GroupSetting, group_count> s{};
            auto set = [&s](SyntaxGroup group, const char* label, const char* color) {
                s[static_cast<int>(group)] = {label, palette_index(color)};
            };
            set(SyntaxGroup::None,         "None",         "Default");
            set(SyntaxGroup::Keyword,      "Keyword",      "Yellow");
            set(SyntaxGroup::Preprocessor, "Preprocessor", "Light Magenta");
            set(SyntaxGroup::Type,         "Type",         "Light Green");
            set(SyntaxGroup::Function,     "Function",     "Light Cyan");
            set(SyntaxGroup::Variable,     "Variable",     "Default");
            set(SyntaxGroup::Property,     "Property",     "Default");
            set(SyntaxGroup::String,       "String",       "Light Pink");
            set(SyntaxGroup::Special,      "Escape",       "Orange");
            set(SyntaxGroup::Number,       "Number",       "Light Pink");
            set(SyntaxGroup::Constant,     "Constant",     "Light Pink");
            set(SyntaxGroup::Comment,      "Comment",      "Sky Blue");
            set(SyntaxGroup::Operator,     "Operator",     "Default");
            set(SyntaxGroup::Punctuation,  "Punctuation",  "Default");
            set(SyntaxGroup::Tag,          "Tag",          "Cyan");
            set(SyntaxGroup::Heading,      "Heading",      "Light Magenta");
            set(SyntaxGroup::DiffAdded,    "Diff Added",   "Light Green");
            set(SyntaxGroup::DiffRemoved,  "Diff Removed", "Light Red");
            return s;
        }();
        return settings;
    }

    // --- capture name -> group ---

    // "keyword" matches "keyword" and "keyword.return", but not "keywords".
    bool is_or_under(std::string_view name, std::string_view family) {
        if (name.substr(0, family.size()) != family) {
            return false;
        }
        return name.size() == family.size() || name[family.size()] == '.';
    }

    SyntaxGroup group_for_capture(std::string_view name) {
        // specific names first, then the generic families.
        if (is_or_under(name, "comment")) return SyntaxGroup::Comment;

        // markup (markdown, ...): both the current "markup.*" names and the
        // older "text.*" ones.
        if (is_or_under(name, "markup.heading") || is_or_under(name, "text.title")) return SyntaxGroup::Heading;
        if (is_or_under(name, "markup.raw") || is_or_under(name, "text.literal")) return SyntaxGroup::String;
        if (is_or_under(name, "markup.link") || is_or_under(name, "text.uri") || is_or_under(name, "text.reference")) return SyntaxGroup::Tag;
        if (is_or_under(name, "markup.list")) return SyntaxGroup::Special;
        if (is_or_under(name, "markup.quote")) return SyntaxGroup::Comment;
        if (is_or_under(name, "markup.math")) return SyntaxGroup::Number;

        if (is_or_under(name, "diff.plus") || name == "addition" || is_or_under(name, "text.diff.add")) return SyntaxGroup::DiffAdded;
        if (is_or_under(name, "diff.minus") || name == "deletion" || is_or_under(name, "text.diff.delete")) return SyntaxGroup::DiffRemoved;
        if (is_or_under(name, "diff.delta")) return SyntaxGroup::Special;
        if (is_or_under(name, "text.danger") || is_or_under(name, "text.warning") || is_or_under(name, "text.note")) return SyntaxGroup::Special;

        // TextMate-style names used by a few grammars (elm, glsl, ...).
        if (is_or_under(name, "character.escape") || is_or_under(name, "regexp")) return SyntaxGroup::Special;
        if (is_or_under(name, "char")) return SyntaxGroup::String;
        if (is_or_under(name, "identifier.constant")) return SyntaxGroup::Constant;
        if (is_or_under(name, "identifier.parameter") || is_or_under(name, "parameter")) return SyntaxGroup::Variable;
        if (is_or_under(name, "local.function")) return SyntaxGroup::Function;
        if (is_or_under(name, "storage.type") || is_or_under(name, "union")) return SyntaxGroup::Type;
        if (is_or_under(name, "meta.import")) return SyntaxGroup::Keyword;
        if (is_or_under(name, "conditional.ternary")) return SyntaxGroup::Operator;

        if (name == "escape" || is_or_under(name, "string.escape") || is_or_under(name, "string.special.regex") || is_or_under(name, "string.regex") || is_or_under(name, "character.special")) return SyntaxGroup::Special;
        if (name == "string.special.key") return SyntaxGroup::Property;      // json keys
        if (name == "string.special.symbol") return SyntaxGroup::Constant;   // ruby :symbols
        if (is_or_under(name, "string") || name == "character") return SyntaxGroup::String;

        if (is_or_under(name, "number") || name == "float") return SyntaxGroup::Number;
        if (name == "boolean" || is_or_under(name, "constant") || is_or_under(name, "variable.builtin")) return SyntaxGroup::Constant;

        if (name == "function.macro" || name == "preproc" || is_or_under(name, "keyword.directive")) return SyntaxGroup::Preprocessor;
        if (is_or_under(name, "function") || is_or_under(name, "method")) return SyntaxGroup::Function;
        if (is_or_under(name, "type") || is_or_under(name, "constructor") || is_or_under(name, "namespace") || is_or_under(name, "module")) return SyntaxGroup::Type;

        if (is_or_under(name, "property") || name == "field") return SyntaxGroup::Property;
        if (is_or_under(name, "variable") || name == "parameter") return SyntaxGroup::Variable;

        if (is_or_under(name, "keyword")) return SyntaxGroup::Keyword;
        for (std::string_view keyword_like : {"conditional", "repeat", "label", "import", "include", "exception", "storageclass", "charset", "media", "keyframes", "supports"}) {
            if (name == keyword_like) return SyntaxGroup::Keyword;
        }

        if (is_or_under(name, "operator")) return SyntaxGroup::Operator;
        if (is_or_under(name, "punctuation") || name == "delimiter") return SyntaxGroup::Punctuation;
        if (is_or_under(name, "tag") || is_or_under(name, "attribute") || is_or_under(name, "annotation")) return SyntaxGroup::Tag;

        return SyntaxGroup::None; // eg. @embedded, @none
    }

    // --- query predicates ---
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

    std::string query_string(const TSQuery* query, uint32_t id) {
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

    void add_predicate(const TSQuery* query, std::string op, const TSQueryPredicateStep* args, uint32_t arg_count, std::vector<Predicate>& out) {
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
    bool capture_node(const TSQueryMatch& match, uint32_t capture_id, TSNode& out) {
        for (uint16_t i = 0; i < match.capture_count; ++i) {
            if (match.captures[i].index == capture_id) {
                out = match.captures[i].node;
                return true;
            }
        }
        return false;
    }

    std::string_view node_text(TSNode node, const std::string& source) {
        const uint32_t start = std::min<uint32_t>(ts_node_start_byte(node), source.size());
        const uint32_t end = std::min<uint32_t>(ts_node_end_byte(node), source.size());
        return std::string_view(source).substr(start, end - start);
    }

    bool type_in(TSNode node, const std::vector<std::string>& types) {
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

    // --- languages ---

    struct LoadedLanguage {
        const TSLanguage* language = nullptr;
        TSQuery* query = nullptr;                       // null if the query failed to compile
        std::vector<SyntaxGroup> capture_groups;        // by capture id
        std::vector<bool> capture_paints;               // by capture id, false for helper captures
        std::vector<std::vector<Predicate>> predicates; // by pattern index

        ~LoadedLanguage() {
            if (query) {
                ts_query_delete(query);
            }
        }
    };

    // languages are compiled the first time a file of that language is shown.
    LoadedLanguage* loaded_language(std::size_t index) {
        static std::vector<std::unique_ptr<LoadedLanguage>> loaded(syntax_language_count);

        if (loaded[index]) {
            return loaded[index].get();
        }

        auto lang = std::make_unique<LoadedLanguage>();
        const SyntaxLanguage& info = syntax_languages[index];
        lang->language = info.language();

        uint32_t error_offset = 0;
        TSQueryError error_type = TSQueryErrorNone;
        const std::string_view source = info.highlight_query;
        lang->query = ts_query_new(lang->language, source.data(), static_cast<uint32_t>(source.size()), &error_offset, &error_type);

        if (!lang->query) {
            current_events = std::string("Syntax highlighting: the ") + info.name + " query failed to load (byte " + std::to_string(error_offset) + ").";
        } else {
            const uint32_t capture_count = ts_query_capture_count(lang->query);
            for (uint32_t id = 0; id < capture_count; ++id) {
                uint32_t length = 0;
                const char* name_data = ts_query_capture_name_for_id(lang->query, id, &length); // sets length
                const std::string_view name(name_data, length);
                const SyntaxGroup group = group_for_capture(name);
                lang->capture_groups.push_back(group);
                // "@_name" captures only feed predicates. @none / @embedded do
                // paint: they reset the color of what they cover (eg. `${x}`).
                lang->capture_paints.push_back(name.substr(0, 1) != "_" && (group != SyntaxGroup::None || name == "none" || name == "embedded"));
            }

            const uint32_t pattern_count = ts_query_pattern_count(lang->query);
            for (uint32_t pattern = 0; pattern < pattern_count; ++pattern) {
                lang->predicates.push_back(read_predicates(lang->query, pattern));
            }
        }

        loaded[index] = std::move(lang);
        return loaded[index].get();
    }

    int language_index_for(const std::string& path) {
        if (path.empty()) {
            return -1;
        }

        const fs::path p(path);
        const std::string file_name = p.filename().string();
        std::string extension = p.extension().string(); // ".bashrc" has none, it is matched by name.
        if (!extension.empty()) {
            extension.erase(0, 1);
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        }

        for (std::size_t i = 0; i < syntax_language_count; ++i) {
            const std::string_view match = syntax_languages[i].match;
            if (!extension.empty() && match.find(" " + extension + " ") != std::string_view::npos) {
                return static_cast<int>(i);
            }
            if (match.find(" " + file_name + " ") != std::string_view::npos) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // --- highlighting ---

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

    TSParser* shared_parser() {
        static TSParser* parser = ts_parser_new();
        return parser;
    }

    TSPoint point_at(const std::string& text, std::size_t byte) {
        const auto begin = text.begin();
        const auto row = std::count(begin, begin + static_cast<std::ptrdiff_t>(byte), '\n');
        const std::size_t line_start = text.rfind('\n', byte == 0 ? 0 : byte - 1);
        const std::size_t column = (byte == 0 || line_start == std::string::npos) ? byte : byte - line_start - 1;
        return {static_cast<uint32_t>(row), static_cast<uint32_t>(column)};
    }

    // tell the old tree which bytes changed (the part between the common
    // prefix and suffix), so tree-sitter can reuse everything else.
    void edit_tree(TSTree* tree, const std::string& old_text, const std::string& new_text) {
        const std::size_t max_common = std::min(old_text.size(), new_text.size());

        std::size_t prefix = 0;
        while (prefix < max_common && old_text[prefix] == new_text[prefix]) {
            prefix++;
        }
        std::size_t suffix = 0;
        while (suffix < max_common - prefix && old_text[old_text.size() - 1 - suffix] == new_text[new_text.size() - 1 - suffix]) {
            suffix++;
        }

        TSInputEdit edit;
        edit.start_byte = static_cast<uint32_t>(prefix);
        edit.old_end_byte = static_cast<uint32_t>(old_text.size() - suffix);
        edit.new_end_byte = static_cast<uint32_t>(new_text.size() - suffix);
        edit.start_point = point_at(old_text, edit.start_byte);
        edit.old_end_point = point_at(old_text, edit.old_end_byte);
        edit.new_end_point = point_at(new_text, edit.new_end_byte);
        ts_tree_edit(tree, &edit);
    }

    HighlightCache& highlights_for(const std::string& content, const std::string& path) {
        static HighlightCache cache;

        if (cache.path == path && cache.content == content && (cache.tree || cache.language_index < 0)) {
            return cache;
        }

        const bool same_file = cache.path == path && cache.tree;
        const int language_index = same_file ? cache.language_index : language_index_for(path);

        TSTree* old_tree = nullptr;
        if (same_file) {
            edit_tree(cache.tree, cache.content, content);
            old_tree = cache.tree;
        } else if (cache.tree) {
            ts_tree_delete(cache.tree);
        }
        cache.tree = nullptr;

        cache.path = path;
        cache.content = content;
        cache.language_index = language_index;
        cache.colored_begin = cache.colored_end = 0;
        cache.byte_groups.assign(content.size(), static_cast<unsigned char>(SyntaxGroup::None));

        cache.line_starts.assign(1, 0);
        for (std::size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\n') {
                cache.line_starts.push_back(i + 1);
            }
        }

        if (language_index >= 0) {
            LoadedLanguage* lang = loaded_language(static_cast<std::size_t>(language_index));
            if (lang->query && ts_parser_set_language(shared_parser(), lang->language)) {
                cache.tree = ts_parser_parse_string(shared_parser(), old_tree, content.data(), static_cast<uint32_t>(content.size()));
            }
        }
        if (old_tree) {
            ts_tree_delete(old_tree);
        }
        if (!cache.tree) {
            cache.language_index = -1; // no grammar (or it failed): plain text.
        }
        return cache;
    }

    // run the highlight query over [begin, end) and store the groups.
    void color_range(HighlightCache& cache, std::size_t begin, std::size_t end) {
        LoadedLanguage* lang = loaded_language(static_cast<std::size_t>(cache.language_index));

        struct Span {
            uint32_t start;
            uint32_t end;
            uint32_t pattern;
            SyntaxGroup group;
        };
        std::vector<Span> spans;

        TSQueryCursor* cursor = ts_query_cursor_new();
        ts_query_cursor_set_byte_range(cursor, static_cast<uint32_t>(begin), static_cast<uint32_t>(end));
        ts_query_cursor_exec(cursor, lang->query, ts_tree_root_node(cache.tree));

        TSQueryMatch match;
        while (ts_query_cursor_next_match(cursor, &match)) {
            if (!predicates_pass(lang->predicates[match.pattern_index], match, cache.content)) {
                continue;
            }
            for (uint16_t i = 0; i < match.capture_count; ++i) {
                const TSQueryCapture& capture = match.captures[i];
                if (!lang->capture_paints[capture.index]) {
                    continue;
                }
                SyntaxGroup group = lang->capture_groups[capture.index];
                // the C/C++ queries tag "#include", "#define", ... as plain
                // @keyword; give them the preprocessor color like vim does.
                if (group == SyntaxGroup::Keyword && ts_node_type(capture.node)[0] == '#') {
                    group = SyntaxGroup::Preprocessor;
                }
                spans.push_back({ts_node_start_byte(capture.node), ts_node_end_byte(capture.node), match.pattern_index, group});
            }
        }
        ts_query_cursor_delete(cursor);

        // painted in order, each span over the previous ones: outer nodes first
        // so inner nodes win, and for the very same node the later pattern in
        // the query wins (tree-sitter-highlight's rule; the upstream queries
        // put generic patterns such as `(identifier) @variable` first).
        std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) {
            if (a.start != b.start) return a.start < b.start;
            if (a.end != b.end) return a.end > b.end;
            return a.pattern < b.pattern;
        });

        std::fill(cache.byte_groups.begin() + begin, cache.byte_groups.begin() + end, static_cast<unsigned char>(SyntaxGroup::None));
        for (const Span& span : spans) {
            // spans can reach outside the range (eg. a long comment); clip them.
            const std::size_t span_begin = std::max<std::size_t>(span.start, begin);
            const std::size_t span_end = std::min<std::size_t>(span.end, end);
            if (span_begin < span_end) {
                std::fill(cache.byte_groups.begin() + span_begin, cache.byte_groups.begin() + span_end, static_cast<unsigned char>(span.group));
            }
        }

        cache.colored_begin = begin;
        cache.colored_end = end;
    }

    // make sure [begin, end) has colors. A margin around it is colored too, so
    // small scrolls don't re-run the query.
    void ensure_colored(HighlightCache& cache, std::size_t begin, std::size_t end) {
        if (begin >= cache.colored_begin && end <= cache.colored_end) {
            return;
        }
        constexpr std::size_t margin = 16 * 1024;
        color_range(cache, begin > margin ? begin - margin : 0, std::min(end + margin, cache.content.size()));
    }

    // renders the Input, then recolors its cells. The Input draws line `i` of
    // the content on row `box.y_min + i`, one glyph per cell.
    class SyntaxPaint : public ftxui::Node {
    public:
        SyntaxPaint(ftxui::Element child, const std::string& content, const std::string& path)
            : ftxui::Node({std::move(child)}), content_(content), path_(path) {}

        void ComputeRequirement() override {
            ftxui::Node::ComputeRequirement();
            requirement_ = children_[0]->requirement();
        }

        void SetBox(ftxui::Box box) override {
            ftxui::Node::SetBox(box);
            children_[0]->SetBox(box);
        }

        void Render(ftxui::Screen& screen) override {
            ftxui::Node::Render(screen);

            if (!syntax_highlighting || content_.empty()) {
                return;
            }

            HighlightCache& cache = highlights_for(content_, path_);
            if (cache.language_index < 0) {
                return; // no grammar for this file.
            }

            // only the rows/columns visible through the surrounding frame.
            const int y_begin = std::max(box_.y_min, screen.stencil.y_min);
            const int y_end = std::min(box_.y_max, screen.stencil.y_max);

            const std::size_t first_line = static_cast<std::size_t>(std::max(0, y_begin - box_.y_min));
            const std::size_t last_line = static_cast<std::size_t>(std::max(0, y_end - box_.y_min)) + 1;
            if (first_line >= cache.line_starts.size()) {
                return;
            }
            const std::size_t visible_begin = cache.line_starts[first_line];
            const std::size_t visible_end = last_line < cache.line_starts.size() ? cache.line_starts[last_line] : content_.size();
            ensure_colored(cache, visible_begin, visible_end);

            const auto& settings = group_settings();
            const auto& palette = color_palette();

            for (int y = y_begin; y <= y_end; ++y) {
                const std::size_t line = static_cast<std::size_t>(y - box_.y_min);
                if (line >= cache.line_starts.size()) {
                    break;
                }

                const std::size_t line_start = cache.line_starts[line];
                const std::size_t line_end = line + 1 < cache.line_starts.size() ? cache.line_starts[line + 1] - 1 : content_.size();
                const std::string_view line_text = std::string_view(content_).substr(line_start, line_end - line_start);

                // walk the glyphs exactly like the Input's text() does: control
                // characters (eg. tabs) are dropped and full width glyphs take a
                // second, empty cell. Find each glyph's byte to get its group.
                const std::vector<std::string> glyphs = ftxui::Utf8ToGlyphs(line_text);
                std::size_t byte = 0;
                unsigned char group = 0;

                for (std::size_t cell = 0; cell < glyphs.size(); ++cell) {
                    const std::string& glyph = glyphs[cell];
                    if (!glyph.empty()) {
                        const std::size_t found = line_text.find(glyph, byte);
                        if (found == std::string_view::npos) {
                            break;
                        }
                        byte = found + glyph.size();
                        group = cache.byte_groups[line_start + found];
                    }

                    const int x = box_.x_min + static_cast<int>(cell);
                    if (x < screen.stencil.x_min) {
                        continue;
                    }
                    if (x > screen.stencil.x_max) {
                        break;
                    }

                    const int color_index = settings[group].color_index;
                    if (group != 0 && color_index != 0) {
                        screen.CellAt(x, y).foreground_color = palette[static_cast<std::size_t>(color_index)].color;
                    }
                }
            }
        }

    private:
        const std::string& content_;
        const std::string& path_;
    };

    // one View tab row: "Keyword       < Yellow >". Left/Right (or h/l) change
    // the color, Enter or a click moves to the next one.
    class SyntaxColorRow : public ftxui::ComponentBase {
    public:
        explicit SyntaxColorRow(SyntaxGroup group) : group_(static_cast<int>(group)) {}

        ftxui::Element OnRender() override {
            const GroupSetting& setting = group_settings()[group_];
            const NamedColor& named = color_palette()[static_cast<std::size_t>(setting.color_index)];

            std::string label = setting.label;
            label.resize(std::max<std::size_t>(label.size(), 14), ' ');

            ftxui::Element sample = ftxui::text(named.name);
            if (setting.color_index != 0) {
                sample = ftxui::color(named.color, sample);
            }

            ftxui::Element e = ftxui::hbox({ftxui::text(label), ftxui::text("< "), sample, ftxui::text(" >")});
            if (Focused()) {
                e = ftxui::bold(e);
            }
            return e | ftxui::reflect(box_);
        }

        bool OnEvent(ftxui::Event event) override {
            if (event.is_mouse()) {
                const ftxui::Mouse& mouse = event.mouse();
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed && box_.Contain(mouse.x, mouse.y)) {
                    TakeFocus();
                    cycle(+1);
                    return true;
                }
                return false;
            }

            if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Character('l') || event == ftxui::Event::Return) {
                cycle(+1);
                return true;
            }
            if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::Character('h')) {
                cycle(-1);
                return true;
            }
            return false;
        }

        bool Focusable() const override { return true; }

    private:
        void cycle(int step) {
            const int size = static_cast<int>(color_palette().size());
            int& index = group_settings()[group_].color_index;
            index = (index + step + size) % size;
        }

        int group_;
        ftxui::Box box_;
    };

} // namespace

std::string syntax_language_name(const std::string& path) {
    const int index = language_index_for(path);
    return index < 0 ? "" : syntax_languages[index].name;
}

ftxui::Decorator syntax_highlight(const std::string& content, const std::string& path) {
    return [&content, &path](ftxui::Element child) -> ftxui::Element {
        return std::make_shared<SyntaxPaint>(std::move(child), content, path);
    };
}

ftxui::Component syntax_color_settings_func() {
    ftxui::Components rows;
    for (int group = 1; group < group_count; ++group) {
        auto row = std::make_shared<SyntaxColorRow>(static_cast<SyntaxGroup>(group));

        rows.push_back(ftxui::Renderer(row, [row] {
            return ftxui::hbox({
                ftxui::text("    "),// indent
                row->Render(),
            });
        }));
    }
    ftxui::Component rows_container = ftxui::Container::Vertical(std::move(rows));

    // the header is drawn around the rows (not a row itself), so Up/Down
    // never stop on it.
    return ftxui::Renderer(rows_container, [rows_container] {
        const std::string language = syntax_language_name(long_active_file_name);
        return ftxui::vbox({
            ftxui::text(""),
            ftxui::bold(ftxui::text("Syntax Colors")),
            ftxui::dim(ftxui::text("Current file: " + (language.empty() ? std::string("no highlighting") : language))),
            ftxui::dim(ftxui::text("Left / Right or click to change a color.")),
            ftxui::text(""),
            rows_container->Render(),
        });
    });
}
