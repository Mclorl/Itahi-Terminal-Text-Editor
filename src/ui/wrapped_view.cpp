#include "ui/wrapped_view.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <utility>
#include <string_view>
#include <vector>

#include <ftxui/screen/string.hpp>

#include "syntax/highlighter.hpp"
#include "editor/search.hpp"
#include "ui/search_paint.hpp"
#include "ui/widgets.hpp"

namespace {

    // one glyph of a line: where it starts in the text, and its cells.
    struct Glyph {
        std::size_t byte;
        std::size_t size;
        int width;  // 1, or 2 for full-width characters
    };

    // the glyphs of `line` (starting at byte `offset` of the text), the way
    // ftxui::text() draws them: control characters such as tabs are dropped.
    std::vector<Glyph> glyphs_of(std::string_view line, std::size_t offset) {
        std::vector<Glyph> out;
        const std::vector<std::string> cells = ftxui::Utf8ToGlyphs(line);
        std::size_t byte = 0;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (cells[i].empty()) {
                continue;  // 2nd cell of a full-width glyph
            }
            const std::size_t found = line.find(cells[i], byte);
            if (found == std::string_view::npos) {
                break;
            }
            const int width = i + 1 < cells.size() && cells[i + 1].empty() ? 2 : 1;
            out.push_back({offset + found, cells[i].size(), width});
            byte = found + cells[i].size();
        }
        return out;
    }

    // splits glyphs [0, n) into rows of at most `width` cells, breaking after
    // the last space of a row when there is one. Returns the first glyph of
    // every row.
    std::vector<std::size_t> row_starts(const std::vector<Glyph>& glyphs, std::string_view text, int width) {
        std::vector<std::size_t> starts{0};
        int used = 0;
        std::size_t last_space = std::string::npos;
        for (std::size_t i = starts.back(); i < glyphs.size(); ++i) {
            if (used + glyphs[i].width > width && i > starts.back()) {
                const std::size_t start = last_space != std::string::npos && last_space + 1 > starts.back() ? last_space + 1 : i;
                starts.push_back(start);
                used = 0;
                for (std::size_t k = start; k < i; ++k) {
                    used += glyphs[k].width;
                }
                last_space = std::string::npos;
            }
            used += glyphs[i].width;
            if (text[glyphs[i].byte] == ' ') {
                last_space = i;
            }
        }
        return starts;
    }

    // where every row of the last drawn wrapped view is, for mouse clicks.
    struct RowLayout {
        ftxui::Box box;                                 // the row's text area on screen
        std::vector<std::pair<std::size_t, int>> cells; // (byte, width) of every glyph
        std::size_t end_byte;                           // where a click past the end goes
    };
    std::deque<RowLayout> layout;  // deque: the boxes must not move while rows are added

}  // namespace

std::optional<std::size_t> wrapped_byte_at(int x, int y) {
    if (layout.empty()) {
        return std::nullopt;
    }
    for (const RowLayout& row : layout) {
        if (y < row.box.y_min || y > row.box.y_max) {
            continue;
        }
        int cell_x = row.box.x_min;
        for (const auto& [byte, width] : row.cells) {
            if (x < cell_x + width) {
                return byte;
            }
            cell_x += width;
        }
        return row.end_byte;
    }
    // below the last row: the end of the text.
    if (y > layout.back().box.y_max) {
        return layout.back().end_byte;
    }
    return std::nullopt;
}

int wrapped_gutter_width(const std::string& text, bool show_numbers) {
    if (!show_numbers) {
        return 0;
    }
    const std::size_t lines = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
    return static_cast<int>(std::max<std::size_t>(4, std::to_string(lines).size()));
}

ftxui::Element wrapped_editor_view(const std::string& text, const std::string& path, int cursor, int width,
                                   bool show_numbers, bool underline_active, bool underline_line) {
    width = std::max(width - 1, 1);  // one cell for the cursor at the end of a row
    const int gutter = wrapped_gutter_width(text, show_numbers);
    const std::size_t cursor_byte = static_cast<std::size_t>(std::clamp(cursor, 0, static_cast<int>(text.size())));

    std::vector<ftxui::Color> colors;
    syntax_colors(text, path, colors);

    // one element per visual row, the number and the text side by side, so
    // they can never drift apart (an empty line is still one row high).
    ftxui::Elements rows;
    layout.clear();

    std::size_t line_start = 0;
    for (std::size_t line_number = 1; ; ++line_number) {
        std::size_t line_end = text.find('\n', line_start);
        const bool last_line = line_end == std::string::npos;
        if (last_line) {
            line_end = text.size();
        }
        const std::string_view line = std::string_view(text).substr(line_start, line_end - line_start);
        const std::vector<Glyph> glyphs = glyphs_of(line, line_start);
        const std::vector<std::size_t> starts = row_starts(glyphs, text, width);
        const bool cursor_on_line = cursor_byte >= line_start && cursor_byte <= line_end;

        for (std::size_t r = 0; r < starts.size(); ++r) {
            const std::size_t first = starts[r];
            const std::size_t end = r + 1 < starts.size() ? starts[r + 1] : glyphs.size();

            // runs of the same color (and search highlight) become one text element.
            ftxui::Elements runs;
            bool row_has_cursor = false;
            std::string run;
            ftxui::Color run_color = ftxui::Color::Default;
            int run_kind = 0;
            auto flush = [&] {
                if (!run.empty()) {
                    ftxui::Element e = ftxui::text(run);
                    if (run_kind != 0) {
                        e = ftxui::bgcolor(run_kind == 2 ? search_current_background : search_match_background,
                                           ftxui::color(search_match_foreground, e));
                    } else if (run_color != ftxui::Color::Default) {
                        e = ftxui::color(run_color, e);
                    }
                    runs.push_back(e);
                    run.clear();
                }
            };
            for (std::size_t g = first; g < end; ++g) {
                const std::string glyph = text.substr(glyphs[g].byte, glyphs[g].size);
                const ftxui::Color c = colors.empty() ? ftxui::Color::Default : colors[glyphs[g].byte];
                const int kind = search_match_kind(glyphs[g].byte);
                if (cursor_on_line && glyphs[g].byte == cursor_byte) {
                    flush();
                    runs.push_back(ftxui::inverted(ftxui::text(glyph)) | ftxui::focus);
                    row_has_cursor = true;
                    continue;
                }
                if (c != run_color || kind != run_kind) {
                    flush();
                    run_color = c;
                    run_kind = kind;
                }
                run += glyph;
            }
            flush();
            // the cursor after the last character of the line.
            if (cursor_on_line && r + 1 == starts.size() && !row_has_cursor &&
                (glyphs.empty() || cursor_byte >= glyphs.back().byte + glyphs.back().size)) {
                runs.push_back(ftxui::inverted(ftxui::text(" ")) | ftxui::focus);
            }
            if (runs.empty()) {
                runs.push_back(ftxui::text(""));
            }

            // remember where this row's glyphs are, for mouse clicks. A click
            // past the end of a row goes to its end: the end of the line on
            // its last row, otherwise its last glyph.
            RowLayout& row_layout = layout.emplace_back();
            for (std::size_t g = first; g < end; ++g) {
                row_layout.cells.emplace_back(glyphs[g].byte, glyphs[g].width);
            }
            row_layout.end_byte = r + 1 == starts.size() ? line_end
                                : end > first ? glyphs[end - 1].byte : line_start;

            ftxui::Element row_text = ftxui::hbox(std::move(runs)) | ftxui::xflex | capture_box(row_layout.box);
            if (cursor_on_line && underline_line) {
                row_text = underlined_white(row_text);
            }

            if (gutter == 0) {
                rows.push_back(row_text);
                continue;
            }
            std::string number = r == 0 ? std::to_string(line_number) : "";
            number = std::string(static_cast<std::size_t>(gutter) - number.size(), ' ') + number;
            ftxui::Element n = ftxui::text(number);
            if (cursor_on_line && r == 0) {
                n = ftxui::bold(ftxui::color(ftxui::Color::Yellow, underline_active ? ftxui::underlined(n) : n));
            } else {
                n = ftxui::dim(n);
            }
            rows.push_back(ftxui::hbox({n, ftxui::text(" "), row_text}));
        }

        if (last_line) {
            break;
        }
        line_start = line_end + 1;
    }

    return ftxui::vbox(std::move(rows));
}
