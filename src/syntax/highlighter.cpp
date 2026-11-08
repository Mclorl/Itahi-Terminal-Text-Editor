#include "syntax/highlighter.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "editor/state.hpp"
#include "syntax/languages.hpp"
#include "syntax/predicates.hpp"
#include "syntax/theme.hpp"

namespace fs = std::filesystem;

namespace {

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
            command_error = std::string("Syntax highlighting: the ") + info.name + " query failed to load (byte " + std::to_string(error_offset) + ").";
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

}  // namespace

static TSParser* shared_parser() {
    static TSParser* parser = ts_parser_new();
    return parser;
}

static TSPoint point_at(const std::string& text, std::size_t byte) {
    const auto begin = text.begin();
    const auto row = std::count(begin, begin + static_cast<std::ptrdiff_t>(byte), '\n');
    const std::size_t line_start = text.rfind('\n', byte == 0 ? 0 : byte - 1);
    const std::size_t column = (byte == 0 || line_start == std::string::npos) ? byte : byte - line_start - 1;
    return {static_cast<uint32_t>(row), static_cast<uint32_t>(column)};
}

// tell the old tree which bytes changed (the part between the common
// prefix and suffix), so tree-sitter can reuse everything else.
static void edit_tree(TSTree* tree, const std::string& old_text, const std::string& new_text) {
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

HighlightCache& highlights_for(const std::string& text, const std::string& path) {
    static HighlightCache cache;

    if (cache.path == path && cache.content == text && (cache.tree || cache.language_index < 0)) {
        return cache;
    }

    const bool same_file = cache.path == path && cache.tree;
    const int language_index = same_file ? cache.language_index : language_index_for(path);

    TSTree* old_tree = nullptr;
    if (same_file) {
        edit_tree(cache.tree, cache.content, text);
        old_tree = cache.tree;
    } else if (cache.tree) {
        ts_tree_delete(cache.tree);
    }
    cache.tree = nullptr;

    cache.path = path;
    cache.content = text;
    cache.language_index = language_index;
    cache.colored_begin = cache.colored_end = 0;
    cache.byte_groups.assign(text.size(), static_cast<unsigned char>(SyntaxGroup::None));

    cache.line_starts.assign(1, 0);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            cache.line_starts.push_back(i + 1);
        }
    }

    if (language_index >= 0) {
        LoadedLanguage* lang = loaded_language(static_cast<std::size_t>(language_index));
        if (lang->query && ts_parser_set_language(shared_parser(), lang->language)) {
            cache.tree = ts_parser_parse_string(shared_parser(), old_tree, text.data(), static_cast<uint32_t>(text.size()));
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
static void color_range(HighlightCache& cache, std::size_t begin, std::size_t end) {
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

void syntax_colors(const std::string& text, const std::string& path, std::vector<ftxui::Color>& out) {
    out.assign(text.size(), ftxui::Color::Default);
    if (!syntax_highlighting || text.empty()) {
        return;
    }
    HighlightCache& cache = highlights_for(text, path);
    if (cache.language_index < 0) {
        return;
    }
    ensure_colored(cache, 0, text.size());

    const auto& settings = group_settings();
    const auto& palette = color_palette();
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char group = cache.byte_groups[i];
        const int color_index = settings[group].color_index;
        if (group != 0 && color_index != 0) {
            out[i] = palette[static_cast<std::size_t>(color_index)].color;
        }
    }
}

std::string syntax_language_name(const std::string& path) {
    const int index = language_index_for(path);
    return index < 0 ? "" : syntax_languages[index].name;
}
