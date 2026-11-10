#include "syntax/highlighter.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "editor/settings.hpp"
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

    // a capture name and the group it is drawn as. `family`: the name and
    // everything under it ("keyword" covers "keyword.return"); otherwise the
    // name alone.
    struct CaptureRule {
        std::string_view name;
        bool family;
        SyntaxGroup group;
    };

    // checked in order, the first rule that fits wins: specific names first,
    // then the generic families.
    constexpr CaptureRule capture_rules[] = {
        {"comment", true, SyntaxGroup::Comment},

        // markup (markdown, ...): both the current "markup.*" names and the
        // older "text.*" ones.
        {"markup.heading", true, SyntaxGroup::Heading},
        {"text.title", true, SyntaxGroup::Heading},
        {"markup.raw", true, SyntaxGroup::String},
        {"text.literal", true, SyntaxGroup::String},
        {"markup.link", true, SyntaxGroup::Tag},
        {"text.uri", true, SyntaxGroup::Tag},
        {"text.reference", true, SyntaxGroup::Tag},
        {"markup.list", true, SyntaxGroup::Special},
        {"markup.quote", true, SyntaxGroup::Comment},
        {"markup.math", true, SyntaxGroup::Number},

        {"diff.plus", true, SyntaxGroup::DiffAdded},
        {"addition", false, SyntaxGroup::DiffAdded},
        {"text.diff.add", true, SyntaxGroup::DiffAdded},
        {"diff.minus", true, SyntaxGroup::DiffRemoved},
        {"deletion", false, SyntaxGroup::DiffRemoved},
        {"text.diff.delete", true, SyntaxGroup::DiffRemoved},
        {"diff.delta", true, SyntaxGroup::Special},
        {"text.danger", true, SyntaxGroup::Special},
        {"text.warning", true, SyntaxGroup::Special},
        {"text.note", true, SyntaxGroup::Special},

        // TextMate-style names used by a few grammars (elm, glsl, ...).
        {"character.escape", true, SyntaxGroup::Special},
        {"regexp", true, SyntaxGroup::Special},
        {"char", true, SyntaxGroup::String},
        {"identifier.constant", true, SyntaxGroup::Constant},
        {"identifier.parameter", true, SyntaxGroup::Variable},
        {"parameter", true, SyntaxGroup::Variable},
        {"local.function", true, SyntaxGroup::Function},
        {"storage.type", true, SyntaxGroup::Type},
        {"union", true, SyntaxGroup::Type},
        {"meta.import", true, SyntaxGroup::Keyword},
        {"conditional.ternary", true, SyntaxGroup::Operator},

        {"escape", false, SyntaxGroup::Special},
        {"string.escape", true, SyntaxGroup::Special},
        {"string.special.regex", true, SyntaxGroup::Special},
        {"string.regex", true, SyntaxGroup::Special},
        {"character.special", true, SyntaxGroup::Special},
        {"string.special.key", false, SyntaxGroup::Property},     // json keys
        {"string.special.symbol", false, SyntaxGroup::Constant},  // ruby :symbols
        {"string", true, SyntaxGroup::String},
        {"character", false, SyntaxGroup::String},

        {"number", true, SyntaxGroup::Number},
        {"float", false, SyntaxGroup::Number},
        {"boolean", false, SyntaxGroup::Constant},
        {"constant", true, SyntaxGroup::Constant},
        {"variable.builtin", true, SyntaxGroup::Constant},

        {"function.macro", false, SyntaxGroup::Preprocessor},
        {"preproc", false, SyntaxGroup::Preprocessor},
        {"keyword.directive", true, SyntaxGroup::Preprocessor},
        {"function", true, SyntaxGroup::Function},
        {"method", true, SyntaxGroup::Function},
        {"type", true, SyntaxGroup::Type},
        {"constructor", true, SyntaxGroup::Type},
        {"namespace", true, SyntaxGroup::Type},
        {"module", true, SyntaxGroup::Type},

        {"property", true, SyntaxGroup::Property},
        {"field", false, SyntaxGroup::Property},
        {"variable", true, SyntaxGroup::Variable},

        {"keyword", true, SyntaxGroup::Keyword},
        {"conditional", false, SyntaxGroup::Keyword},
        {"repeat", false, SyntaxGroup::Keyword},
        {"label", false, SyntaxGroup::Keyword},
        {"import", false, SyntaxGroup::Keyword},
        {"include", false, SyntaxGroup::Keyword},
        {"exception", false, SyntaxGroup::Keyword},
        {"storageclass", false, SyntaxGroup::Keyword},
        {"charset", false, SyntaxGroup::Keyword},
        {"media", false, SyntaxGroup::Keyword},
        {"keyframes", false, SyntaxGroup::Keyword},
        {"supports", false, SyntaxGroup::Keyword},

        {"operator", true, SyntaxGroup::Operator},
        {"punctuation", true, SyntaxGroup::Punctuation},
        {"delimiter", false, SyntaxGroup::Punctuation},
        {"tag", true, SyntaxGroup::Tag},
        {"attribute", true, SyntaxGroup::Tag},
        {"annotation", true, SyntaxGroup::Tag},
    };

    SyntaxGroup group_for_capture(std::string_view name) {
        for (const CaptureRule& rule : capture_rules) {
            if (rule.family ? is_or_under(name, rule.name) : name == rule.name) {
                return rule.group;
            }
        }
        return SyntaxGroup::None;  // eg. @embedded, @none
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

static TSPoint ts_point(const TextPoint& point) {
    return {static_cast<uint32_t>(point.row), static_cast<uint32_t>(point.column)};
}

HighlightCache::~HighlightCache() {
    if (tree) {
        ts_tree_delete(tree);
    }
}

namespace {

    // the caches of the files shown most recently, the latest first. A few
    // are kept so switching between tabs doesn't parse the files again.
    constexpr std::size_t max_cached_files = 8;
    // colored parts kept per file: one for each window that can show it.
    constexpr std::size_t max_colored_parts = 4;

    std::vector<std::unique_ptr<HighlightCache>>& caches() {
        static std::vector<std::unique_ptr<HighlightCache>> list;
        return list;
    }

}  // namespace

void forget_highlights(const std::string& path) {
    auto& list = caches();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const auto& c) { return c->path == path; }), list.end());
}

HighlightCache& highlights_for(const std::string& text, const std::string& path, std::uint64_t document_id,
                               std::uint64_t version, const EditsSince& edits_since) {
    auto& list = caches();
    auto found = std::find_if(list.begin(), list.end(), [&](const auto& c) { return c->path == path; });
    if (found == list.end()) {
        list.insert(list.begin(), std::make_unique<HighlightCache>());
        list.front()->path = path;
        list.front()->document_id = document_id;
        list.front()->language_index = language_index_for(path);
        list.front()->colored.reserve(max_colored_parts);
        if (list.size() > max_cached_files) {
            list.pop_back();
        }
        found = list.begin();
    } else if (found != list.begin()) {
        std::rotate(list.begin(), found, found + 1);  // most recent first
        found = list.begin();
    }
    HighlightCache& cache = **found;

    // the file was closed and opened again: nothing known about it holds.
    if (cache.document_id != document_id) {
        if (cache.tree) {
            ts_tree_delete(cache.tree);
            cache.tree = nullptr;
        }
        cache.document_id = document_id;
        cache.version = UINT64_MAX;
        cache.language_index = language_index_for(path);
    }
    if (cache.version == version) {
        return cache;
    }

    TSTree* old_tree = cache.tree;
    cache.tree = nullptr;
    cache.colored.clear();
    cache.too_large = text.size() > max_highlight_bytes;

    if (cache.language_index >= 0 && !cache.too_large) {
        // tell the old tree what changed, so tree-sitter parses only that
        // again. Changes nobody can list: parse from the start.
        std::vector<TextEdit> edits;
        if (old_tree && edits_since && cache.version != UINT64_MAX && edits_since(cache.version, edits)) {
            for (const TextEdit& edit : edits) {
                TSInputEdit input;
                input.start_byte = static_cast<uint32_t>(edit.start);
                input.old_end_byte = static_cast<uint32_t>(edit.old_end);
                input.new_end_byte = static_cast<uint32_t>(edit.new_end);
                input.start_point = ts_point(edit.start_point);
                input.old_end_point = ts_point(edit.old_end_point);
                input.new_end_point = ts_point(edit.new_end_point);
                ts_tree_edit(old_tree, &input);
            }
        } else if (old_tree) {
            ts_tree_delete(old_tree);
            old_tree = nullptr;
        }

        LoadedLanguage* lang = loaded_language(static_cast<std::size_t>(cache.language_index));
        if (lang->query && ts_parser_set_language(shared_parser(), lang->language)) {
            cache.tree = ts_parser_parse_string(shared_parser(), old_tree, text.data(), static_cast<uint32_t>(text.size()));
        }
        if (!cache.tree) {
            cache.language_index = -1;  // the grammar failed: plain text.
        }
    }
    if (old_tree) {
        ts_tree_delete(old_tree);
    }
    cache.version = version;
    return cache;
}

// run the highlight query over the part and store the groups.
static void color_part(HighlightCache& cache, HighlightCache::Colored& part, const std::string& text) {
    LoadedLanguage* lang = loaded_language(static_cast<std::size_t>(cache.language_index));
    const std::size_t begin = part.begin;
    const std::size_t end = part.end;

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
        if (!predicates_pass(lang->predicates[match.pattern_index], match, text)) {
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

    part.groups.assign(end - begin, static_cast<unsigned char>(SyntaxGroup::None));
    for (const Span& span : spans) {
        // spans can reach outside the part (eg. a long comment); clip them.
        const std::size_t span_begin = std::max<std::size_t>(span.start, begin);
        const std::size_t span_end = std::min<std::size_t>(span.end, end);
        if (span_begin < span_end) {
            std::fill(part.groups.begin() + static_cast<std::ptrdiff_t>(span_begin - begin),
                      part.groups.begin() + static_cast<std::ptrdiff_t>(span_end - begin), static_cast<unsigned char>(span.group));
        }
    }
}

const HighlightCache::Colored& colored_part(HighlightCache& cache, const std::string& text, std::size_t begin, std::size_t end) {
    end = std::min(end, text.size());
    begin = std::min(begin, end);
    ++cache.uses;
    for (HighlightCache::Colored& part : cache.colored) {
        if (begin >= part.begin && end <= part.end) {
            part.last_used = cache.uses;
            return part;
        }
    }
    // a new part, in the place of the one unused for the longest when there
    // are enough of them (two windows far apart in one file keep one each).
    HighlightCache::Colored* part = nullptr;
    if (cache.colored.size() < max_colored_parts) {
        part = &cache.colored.emplace_back();
    } else {
        part = &*std::min_element(cache.colored.begin(), cache.colored.end(),
                                  [](const auto& a, const auto& b) { return a.last_used < b.last_used; });
    }
    constexpr std::size_t margin = 16 * 1024;
    part->begin = begin > margin ? begin - margin : 0;
    part->end = std::min(end + margin, text.size());
    part->last_used = cache.uses;
    color_part(cache, *part, text);
    return *part;
}

std::string syntax_language_name(const std::string& path) {
    const int index = language_index_for(path);
    return index < 0 ? "" : syntax_languages[index].name;
}
