#include "ui/status_bar.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "editor/document.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "syntax/highlighter.hpp"
#include "syntax/theme.hpp"
#include "ui/glyphs.hpp"
#include "ui/ui_state.hpp"

namespace fs = std::filesystem;

namespace {

    // the path as typed in commands: relative to the opened folder, without "./".
    std::string shown_path(const std::string& path) {
        const fs::path relative = fs::path(path).lexically_relative(workspace.root);
        if (relative.empty() || *relative.begin() == "..") {
            return path;
        }
        return relative.string();
    }

    std::string git_summary() {
        if (!git_status.in_repository || git_status.branch.empty()) {
            return "";
        }
        std::string text = "git " + git_status.branch;
        if (git_status.ahead > 0) {
            text += " ↑" + std::to_string(git_status.ahead);
        }
        if (git_status.behind > 0) {
            text += " ↓" + std::to_string(git_status.behind);
        }
        const int changed = git_status.changed_count();
        if (changed > 0) {
            text += " +" + std::to_string(changed);
        }
        return text;
    }

}  // namespace

ftxui::Element status_bar_element() {
    const char* mode_name = mode == Mode::Normal ? " NORMAL " : mode == Mode::Edit ? " EDIT " : " COMMAND ";
    ftxui::Elements left{ftxui::text(mode_name)};

    std::vector<std::string> right;
    const Document* document = workspace.active();
    if (document) {
        ftxui::Element file = ftxui::text(" " + shown_path(document->path()) + (document->unsaved() ? "*" : "") + " ");
        left.push_back(file);

        if (document->has_selection()) {
            // counted once per selection, not on every frame: "select all" in
            // a large file is the whole text.
            static std::uint64_t counted_document = 0, counted_version = 0;
            static std::size_t counted_begin = 0, counted_end = 0;
            static std::string counted;
            const auto [begin, end] = document->selection();
            if (counted_document != document->id() || counted_version != document->version() || counted_begin != begin || counted_end != end) {
                const auto from = document->text().begin() + static_cast<std::ptrdiff_t>(begin);
                const auto to = document->text().begin() + static_cast<std::ptrdiff_t>(end);
                const auto characters = std::count_if(from, to, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; });
                const auto lines = std::count(from, to, '\n');
                counted = std::to_string(characters) + " selected" + (lines > 0 ? " (" + std::to_string(lines + 1) + " lines)" : "");
                counted_document = document->id();
                counted_version = document->version();
                counted_begin = begin;
                counted_end = end;
            }
            right.push_back(counted);
        }

        // the column as shown: tabs count as the columns they take.
        const std::size_t line = document->line_of(document->cursor());
        std::vector<GlyphCell> glyphs;
        layout_line(document->text(), document->line_start(line), document->cursor(), get_tab_size(), glyphs);
        right.push_back("Ln " + std::to_string(line + 1) + ", Col " + std::to_string(line_width(glyphs) + 1));

        if (workspace.window_count() > 1) {
            right.push_back("Win " + std::to_string(workspace.window_number(workspace.active_window())) + "/" +
                            std::to_string(workspace.window_count()));
        }
        if (document->disk_seen) {
            right.push_back("changed on disk");
        }
        const std::string language = syntax_language_name(document->path());
        const bool colors_off = !language.empty() && document->text().size() > max_highlight_bytes;
        right.push_back(language.empty() ? "Plain Text" : colors_off ? language + " (large file: no colors)" : language);
        right.push_back((settings.tab_inserts_spaces ? "Spaces: " : "Tab Size: ") + std::to_string(get_tab_size()));
        right.push_back(document->binary() ? "Not UTF-8" : "UTF-8");
        right.push_back(document->line_break() == "\r\n" ? "CRLF" : "LF");
    }
    // Git only while no file is shown: in the editor the row is about the file.
    const std::string git = document ? std::string() : git_summary();
    if (!git.empty()) {
        left.push_back(ftxui::text(" " + git + " "));
    }

    std::string right_text;
    for (const std::string& part : right) {
        right_text += "  " + part;
    }
    left.push_back(ftxui::filler());
    left.push_back(ftxui::text(right_text + "  "));
    return ftxui::dim(ftxui::hbox(std::move(left)));  // quieter than the text above it
}
