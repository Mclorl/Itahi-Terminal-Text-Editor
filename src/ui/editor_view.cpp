#include "ui/editor_view.hpp"

#include <algorithm>
#include <string>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/dom/requirement.hpp>

#include "editor/commands.hpp"
#include "editor/document.hpp"
#include "editor/mode.hpp"
#include "editor/motion.hpp"
#include "editor/search.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/undo_history.hpp"
#include "editor/workspace.hpp"
#include "platform/clipboard.hpp"
#include "syntax/highlighter.hpp"
#include "syntax/theme.hpp"
#include "ui/widgets.hpp"

namespace {

    using ftxui::Event;

    // the patched FTXUI turns a raw LF (Ctrl+J) into this sequence.
    const Event ctrl_j = Event::Special("\x1B[106;5u");

    // keys with Shift (select while moving) and Ctrl (bigger moves), as
    // terminals send them.
    const Event shift_left = Event::Special("\x1B[1;2D");
    const Event shift_right = Event::Special("\x1B[1;2C");
    const Event shift_up = Event::Special("\x1B[1;2A");
    const Event shift_down = Event::Special("\x1B[1;2B");
    const Event shift_home = Event::Special("\x1B[1;2H");
    const Event shift_end = Event::Special("\x1B[1;2F");
    const Event shift_page_up = Event::Special("\x1B[5;2~");
    const Event shift_page_down = Event::Special("\x1B[6;2~");
    const Event ctrl_home = Event::Special("\x1B[1;5H");
    const Event ctrl_end = Event::Special("\x1B[1;5F");
    const Event ctrl_shift_home = Event::Special("\x1B[1;6H");
    const Event ctrl_shift_end = Event::Special("\x1B[1;6F");
    const Event ctrl_shift_left = Event::Special("\x1B[1;6D");
    const Event ctrl_shift_right = Event::Special("\x1B[1;6C");

    std::size_t count_characters(std::string_view text) {
        return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char c) {
            return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
        }));
    }

    // "\r\n" and lone '\r' become '\n', then the document's own line break.
    std::string with_line_breaks(std::string_view text, std::string_view line_break) {
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r') {
                if (i + 1 < text.size() && text[i + 1] == '\n') {
                    ++i;
                }
                out += line_break;
            } else if (text[i] == '\n') {
                out += line_break;
            } else {
                out += text[i];
            }
        }
        return out;
    }

    // the element of the editor: as big as it is given, painted by the view.
    class EditorCanvas : public ftxui::Node {
    public:
        EditorCanvas(EditorView* view, bool focused) : view_(view), focused_(focused) {}

        void ComputeRequirement() override {
            requirement_.min_x = 1;
            requirement_.min_y = 1;
            requirement_.flex_grow_x = requirement_.flex_grow_y = 1;
            requirement_.flex_shrink_x = requirement_.flex_shrink_y = 1;
        }

        void Render(ftxui::Screen& screen) override {
            view_->paint(screen, ftxui::Box::Intersection(box_, screen.stencil), focused_);
        }

    private:
        EditorView* view_;
        bool focused_;
    };

}  // namespace

std::shared_ptr<EditorView> make_editor_view(int window) {
    return std::make_shared<EditorView>(window);
}

ftxui::Element EditorView::OnRender() {
    return std::make_shared<EditorCanvas>(this, Focused());
}

// --- layout --------------------------------------------------------------

const std::vector<GlyphCell>& EditorView::glyphs_of(const Document& document, std::size_t line) {
    const int tab = get_tab_size();
    if (cache_document_ != document.id() || cache_version_ != document.version() || cache_tab_ != tab) {
        line_cache_.clear();
        cache_document_ = document.id();
        cache_version_ = document.version();
        cache_tab_ = tab;
    }
    for (const auto& [cached_line, glyphs] : line_cache_) {
        if (cached_line == line) {
            return glyphs;
        }
    }
    auto& entry = line_cache_.emplace_back(line, std::vector<GlyphCell>{});
    layout_line(document.text(), document.line_start(line), document.line_content_end(line), tab, entry.second);
    return entry.second;
}

std::vector<std::size_t> EditorView::rows_of(const Document& document, std::size_t line) {
    if (!settings.word_wrap) {
        return {0};
    }
    return wrap_rows(glyphs_of(document, line), document.text(), wrap_width_);
}

int EditorView::row_count(const Document& document, std::size_t line) {
    return static_cast<int>(rows_of(document, line).size());
}

int EditorView::column_of(const Document& document, std::size_t byte) {
    const std::vector<GlyphCell>& glyphs = glyphs_of(document, document.line_of(byte));
    for (const GlyphCell& glyph : glyphs) {
        if (byte < glyph.byte + glyph.size) {
            return glyph.column;
        }
    }
    return line_width(glyphs);
}

EditorView::RowPosition EditorView::row_of(const Document& document, std::size_t byte) {
    const std::size_t line = document.line_of(byte);
    if (!settings.word_wrap) {
        return {line, 0};
    }
    const std::vector<GlyphCell>& glyphs = glyphs_of(document, line);
    const std::vector<std::size_t> rows = wrap_rows(glyphs, document.text(), wrap_width_);
    int row = 0;
    for (std::size_t r = 1; r < rows.size(); ++r) {
        if (rows[r] < glyphs.size() && glyphs[rows[r]].byte <= byte) {
            row = static_cast<int>(r);
        }
    }
    return {line, row};
}

std::size_t EditorView::byte_in_row(const Document& document, RowPosition position, int column) {
    const std::vector<GlyphCell>& glyphs = glyphs_of(document, position.line);
    const std::vector<std::size_t> rows = rows_of(document, position.line);
    const std::size_t r = static_cast<std::size_t>(std::clamp(position.row, 0, static_cast<int>(rows.size()) - 1));
    const std::size_t first = rows[r];
    const std::size_t end = r + 1 < rows.size() ? rows[r + 1] : glyphs.size();
    const int origin = first < glyphs.size() ? glyphs[first].column : line_width(glyphs);
    for (std::size_t g = first; g < end; ++g) {
        if (glyphs[g].width > 0 && glyphs[g].column + glyphs[g].width > origin + column) {
            return glyphs[g].byte;
        }
    }
    // past the end of the row: the end of the line, or (a wrapped row) its
    // last glyph, so the cursor stays on that row.
    if (r + 1 == rows.size()) {
        return document.line_content_end(position.line);
    }
    return end > first ? glyphs[end - 1].byte : document.line_start(position.line);
}

EditorView::RowPosition EditorView::move_rows(const Document& document, RowPosition from, int rows) {
    RowPosition at = from;
    while (rows < 0) {
        if (at.row > 0) {
            const int step = std::min(-rows, at.row);
            at.row -= step;
            rows += step;
        } else if (at.line > 0) {
            --at.line;
            at.row = row_count(document, at.line) - 1;
            ++rows;
        } else {
            break;
        }
    }
    while (rows > 0) {
        const int count = row_count(document, at.line);
        if (at.row + 1 < count) {
            const int step = std::min(rows, count - 1 - at.row);
            at.row += step;
            rows -= step;
        } else if (at.line + 1 < document.line_count()) {
            ++at.line;
            at.row = 0;
            --rows;
        } else {
            break;
        }
    }
    return at;
}

std::size_t EditorView::previous_glyph(const Document& document, std::size_t from) {
    const std::size_t line = document.line_of(from);
    if (from <= document.line_start(line)) {
        return line == 0 ? 0 : document.line_content_end(line - 1);  // over the line break
    }
    const std::vector<GlyphCell>& glyphs = glyphs_of(document, line);
    for (auto glyph = glyphs.rbegin(); glyph != glyphs.rend(); ++glyph) {
        if (glyph->byte < from) {
            return glyph->byte;
        }
    }
    return document.line_start(line);
}

std::size_t EditorView::next_glyph(const Document& document, std::size_t from) {
    const std::size_t line = document.line_of(from);
    if (from >= document.line_content_end(line)) {
        return line + 1 < document.line_count() ? document.line_start(line + 1) : document.text().size();
    }
    for (const GlyphCell& glyph : glyphs_of(document, line)) {
        if (from < glyph.byte + glyph.size) {
            return glyph.byte + glyph.size;
        }
    }
    return document.line_content_end(line);
}

// --- scrolling -----------------------------------------------------------

void EditorView::scroll_into_view(Document& document, bool cursor_moved) {
    Document::ViewPosition& view = document.view;
    const std::size_t lines = document.line_count();
    const std::size_t rows = static_cast<std::size_t>(view_rows_);
    const bool centered = settings.scroll_style == SCROLL_KEEP_CURSOR_CENTERED;
    const std::size_t cursor_line = document.line_of(document.cursor());

    if (!settings.word_wrap) {
        view.top_row = 0;
        const std::size_t max_top = lines > rows ? lines - rows : 0;
        if (centered) {
            // like ftxui::frame: the cursor's line in the middle, but no empty
            // rows past the start or the end of the text.
            const std::size_t half = (rows - 1) / 2;
            view.top_line = cursor_line > half ? cursor_line - half : 0;
        } else if (cursor_moved) {
            if (cursor_line < view.top_line) {
                view.top_line = cursor_line;
            } else if (cursor_line >= view.top_line + rows) {
                view.top_line = cursor_line - rows + 1;
            }
        }
        view.top_line = std::min(view.top_line, max_top);

        // sideways: just enough to show the cursor.
        if (cursor_moved || centered) {
            const int column = column_of(document, document.cursor());
            if (column < view.left) {
                view.left = column;
            } else if (column >= view.left + text_columns_) {
                view.left = column - text_columns_ + 1;
            }
        }
        view.left = std::max(0, view.left);
        return;
    }

    view.left = 0;
    view.top_line = std::min(view.top_line, lines - 1);
    view.top_row = std::clamp(view.top_row, 0, row_count(document, view.top_line) - 1);
    const RowPosition cursor = row_of(document, document.cursor());
    RowPosition top{view.top_line, view.top_row};
    if (centered) {
        top = move_rows(document, cursor, -(view_rows_ - 1) / 2);
    } else if (cursor_moved) {
        if (cursor.line < top.line || (cursor.line == top.line && cursor.row < top.row)) {
            top = cursor;
        } else {
            // rows from the top of the view down to the cursor (each line is
            // at least one row, so a far away cursor needs no counting).
            bool below = cursor.line - top.line >= rows;
            if (!below) {
                int count = 0;
                for (RowPosition at = top; !(at.line == cursor.line && at.row == cursor.row);) {
                    const RowPosition next = move_rows(document, at, 1);
                    if (next.line == at.line && next.row == at.row) {
                        break;
                    }
                    at = next;
                    if (++count >= view_rows_) {
                        below = true;
                        break;
                    }
                }
            }
            if (below) {
                top = move_rows(document, cursor, -(view_rows_ - 1));
            }
        }
    }
    // no empty rows at the bottom while the text is longer than the view:
    // the top is never past the one that shows the last row at the bottom.
    const RowPosition end{lines - 1, row_count(document, lines - 1) - 1};
    const RowPosition from_end = move_rows(document, end, -(view_rows_ - 1));
    if (top.line > from_end.line || (top.line == from_end.line && top.row > from_end.row)) {
        top = from_end;
    }
    view.top_line = top.line;
    view.top_row = top.row;
}

void EditorView::scroll_view(Document& document, int rows) {
    Document::ViewPosition& view = document.view;
    if (!settings.word_wrap) {
        const long long top = static_cast<long long>(view.top_line) + rows;
        const long long lines = static_cast<long long>(document.line_count());
        view.top_line = static_cast<std::size_t>(std::clamp<long long>(top, 0, std::max<long long>(0, lines - view_rows_)));
        return;
    }
    const RowPosition top = move_rows(document, {view.top_line, view.top_row}, rows);
    view.top_line = top.line;
    view.top_row = top.row;
}

void EditorView::wheel(int rows) {
    Document* document = this->document();
    if (!document || document->hidden_as_binary()) {
        return;
    }
    const ViewScope scope(*document, window_);  // the wheel also scrolls a window without the focus
    if (settings.scroll_style == SCROLL_FREELY) {
        scroll_view(*document, rows);  // the cursor stays where it is, even off screen
    } else {
        move_vertically(*document, rows, visual_);
    }
    call(hooks.redraw);
}

// --- drawing -------------------------------------------------------------

struct EditorView::Frame {
    ftxui::Screen& screen;
    Document& document;
    const Theme& theme;
    const HighlightCache::Colored* colors;  // nullptr: plain text
    bool focused;       // the terminal's cursor is drawn here
    bool ghost_cursor;  // without the focus: a mark where the cursor is
    int x_min;          // first column of the view
    int gutter;         // columns of the line numbers (0: none)
    int text_x;         // first column of the text
    int right;          // first column past the text
    std::size_t cursor;
    std::size_t cursor_line;
    std::size_t selection_begin;
    std::size_t selection_end;
};

Document* EditorView::document() const {
    return workspace.document_of(window_);
}

// the line number, on the line's first row.
void EditorView::paint_line_number(const Frame& frame, int y, std::size_t line, bool first_row) {
    const bool on_cursor_line = line == frame.cursor_line;
    const std::string number = first_row ? std::to_string(line + 1) : "";
    for (int x = frame.x_min; x < frame.x_min + frame.gutter; ++x) {
        const int digit = x - (frame.x_min + frame.gutter - static_cast<int>(number.size()));
        ftxui::Cell& cell = frame.screen.CellAt(x, y);
        cell.character = digit >= 0 ? std::string(1, number[static_cast<std::size_t>(digit)]) : " ";
        if (on_cursor_line) {
            cell.bold = true;
            cell.foreground_color = frame.theme.accent;
        } else {
            cell.dim = true;
        }
    }
    if (on_cursor_line && first_row && settings.underline_active_row_on_numbers) {
        const int mark = settings.active_row_mark == ROW_MARK_SHADED ? ROW_MARK_SHADED : ROW_MARK_TEXT_COLOR;
        for (int x = frame.x_min; x < frame.x_min + frame.gutter; ++x) {
            mark_active_row_cell(frame.screen, x, y, frame.x_min, mark, frame.theme.shade);
        }
    }
}

// the glyphs [first, end) of a line on row `y`: the character, its syntax
// color, then a search match, then the selection over both.
void EditorView::paint_glyphs(const Frame& frame, int y, const std::vector<GlyphCell>& glyphs, std::size_t first,
                              std::size_t end, int origin, DrawnRow& drawn) {
    const std::string& text = frame.document.text();
    for (std::size_t g = first; g < end; ++g) {
        const GlyphCell& glyph = glyphs[g];
        const int x = frame.text_x + glyph.column - origin;
        if (glyph.width == 0 || x + glyph.width <= frame.text_x) {
            continue;  // not drawn, or left of the view
        }
        if (x >= frame.right) {
            break;
        }
        ftxui::Color foreground = frame.colors ? group_color(frame.colors->group_at(glyph.byte)) : ftxui::Color::Default;
        ftxui::Color background = ftxui::Color::Default;
        if (const int kind = search_match_kind(frame.document, glyph.byte)) {
            background = kind == 2 ? search_current_background : search_match_background;
            foreground = search_match_foreground;
        }
        if (glyph.byte >= frame.selection_begin && glyph.byte < frame.selection_end) {
            background = frame.theme.selection;
        }
        // a tab is blank cells; a wide glyph cut by the edge too.
        const bool whole = x >= frame.text_x && x + glyph.width <= frame.right && text[glyph.byte] != '\t';
        for (int k = 0; k < glyph.width; ++k) {
            if (x + k < frame.text_x || x + k >= frame.right) {
                continue;
            }
            ftxui::Cell& cell = frame.screen.CellAt(x + k, y);
            if (!whole) {
                cell.character = " ";
            } else if (k == 0) {
                cell.character = glyph.invalid ? std::string("\xEF\xBF\xBD") : text.substr(glyph.byte, glyph.size);
            } else {
                cell.character = "";  // 2nd cell of a full-width glyph
            }
            cell.foreground_color = foreground;
            cell.background_color = background;
        }
        drawn.cells.emplace_back(glyph.byte, x);
        drawn.widths.push_back(glyph.width);
    }
}

// the cursor: the terminal's own (a block in Normal mode, a bar in Edit mode)
// in the window that has the focus, an inverted cell in the others.
void EditorView::paint_cursor(const Frame& frame, int y, const std::vector<GlyphCell>& glyphs, std::size_t first,
                              std::size_t end, int origin, bool last_row, std::size_t content_end) {
    int x = -1;
    if (frame.cursor < content_end) {
        for (std::size_t g = first; g < end; ++g) {
            if (glyphs[g].byte <= frame.cursor && frame.cursor < glyphs[g].byte + glyphs[g].size) {
                x = frame.text_x + glyphs[g].column - origin;
                break;
            }
        }
    } else if (last_row) {
        x = frame.text_x + line_width(glyphs) - origin;
    }
    if (x < frame.text_x || x >= frame.right) {
        return;
    }
    if (frame.focused) {
        const auto shape = mode == Mode::Edit ? ftxui::Screen::Cursor::BarBlinking : ftxui::Screen::Cursor::Block;
        frame.screen.SetCursor({x, y, shape});
    } else if (frame.ghost_cursor) {
        frame.screen.CellAt(x, y).inverted = true;
    }
}

// the scroll bar. With word wrap the rows of the lines off screen are
// estimated from their length: exact would mean wrapping the whole file.
void EditorView::paint_scroll(const Frame& frame, const ftxui::Box& box, int bar_x) {
    const Document& document = frame.document;
    const Document::ViewPosition& view = document.view;
    const std::size_t lines = document.line_count();
    int total = static_cast<int>(lines);
    int top = static_cast<int>(view.top_line);
    if (settings.word_wrap) {
        const std::vector<std::size_t>& starts = document.line_starts();
        const std::size_t width = static_cast<std::size_t>(wrap_width_);
        total = 0;
        for (std::size_t line = 0; line < lines; ++line) {
            if (line == view.top_line) {
                top = total + view.top_row;
            }
            const std::size_t length = (line + 1 < lines ? starts[line + 1] - 1 : document.text().size()) - starts[line];
            total += static_cast<int>(std::max<std::size_t>(1, (length + width - 1) / width));
        }
    }
    paint_scroll_bar(frame.screen, bar_x, box.y_min, box.y_max, top, total);
}

void EditorView::paint(ftxui::Screen& screen, const ftxui::Box& box, bool focused) {
    drawn_.clear();
    box_ = box;
    Document* document = this->document();
    if (!document || box.x_max < box.x_min || box.y_max < box.y_min) {
        return;
    }
    // this window's own cursor, selection and scroll position.
    const ViewScope scope(*document, window_);
    if (line_cache_.size() > 512) {
        line_cache_.clear();
    }
    const std::string& text = document->text();
    const std::size_t lines = document->line_count();
    const bool wrap = settings.word_wrap;

    // [line numbers][space][text ...][scroll bar]
    const int gutter = settings.show_line_numbers ? std::max(4, static_cast<int>(std::to_string(lines).size())) : 0;
    const int text_x = box.x_min + (gutter > 0 ? gutter + 1 : 0);
    const int bar_x = box.x_max;
    if (bar_x - text_x < 1) {
        return;
    }
    text_x_ = text_x;
    text_columns_ = bar_x - text_x;
    view_rows_ = box.y_max - box.y_min + 1;
    wrap_width_ = std::max(1, text_columns_ - 1);  // one column for the cursor after a full row

    // the view follows the cursor when it moved. In a window that isn't the
    // active one, an edit made elsewhere is not a move: it keeps showing
    // what it showed.
    const bool active = workspace.active_window() == window_;
    const bool cursor_changed = seen_cursor_ != document->cursor();
    const bool version_changed = seen_version_ != document->version();
    const bool moved = seen_document_ != document->id() ||
                       (active ? cursor_changed || version_changed : cursor_changed && !version_changed);
    seen_document_ = document->id();
    seen_cursor_ = document->cursor();
    seen_version_ = document->version();
    scroll_into_view(*document, moved);
    const Document::ViewPosition& view = document->view;

    // syntax colors, for the part on screen only.
    const HighlightCache::Colored* colors = nullptr;
    if (settings.syntax_highlighting && !text.empty()) {
        HighlightCache& cache = highlights_for(text, document->path(), document->id(), document->version(),
            [document](std::uint64_t since, std::vector<TextEdit>& out) { return document->edits_since(since, out); });
        if (cache.tree) {
            const std::size_t after = std::min(lines, view.top_line + static_cast<std::size_t>(view_rows_) + 1);
            colors = &colored_part(cache, text, document->line_start(view.top_line), after < lines ? document->line_start(after) : text.size());
        }
    }

    const auto [selection_begin, selection_end] = document->selection();
    const Frame frame{screen, *document, current_theme(), colors, focused,
                      !focused && workspace.window_count() > 1,
                      box.x_min, gutter, text_x, text_x + text_columns_,
                      document->cursor(), document->line_of(document->cursor()), selection_begin, selection_end};

    int y = box.y_min;
    for (std::size_t line = view.top_line; line < lines && y <= box.y_max; ++line) {
        const std::vector<GlyphCell>& glyphs = glyphs_of(*document, line);
        const std::size_t content_end = document->line_content_end(line);
        const std::vector<std::size_t> rows = wrap ? wrap_rows(glyphs, text, wrap_width_) : std::vector<std::size_t>{0};
        const bool on_cursor_line = line == frame.cursor_line;
        std::size_t r = line == view.top_line ? std::min(static_cast<std::size_t>(view.top_row), rows.size() - 1) : 0;

        for (; r < rows.size() && y <= box.y_max; ++r, ++y) {
            const std::size_t first = rows[r];
            const std::size_t end = r + 1 < rows.size() ? rows[r + 1] : glyphs.size();
            const bool last_row = r + 1 == rows.size();
            // the column drawn at text_x: the row's first glyph, or the scroll offset.
            const int origin = wrap ? (first < glyphs.size() ? glyphs[first].column : line_width(glyphs)) : view.left;

            if (gutter > 0) {
                paint_line_number(frame, y, line, r == 0);
            }

            DrawnRow& drawn = drawn_.emplace_back();
            drawn.y = y;
            paint_glyphs(frame, y, glyphs, first, end, origin, drawn);
            drawn.end_byte = last_row ? content_end : end > first ? glyphs[end - 1].byte : document->line_start(line);

            // a selected line break: one selected cell after the text.
            if (last_row && line + 1 < lines && selection_begin <= content_end && content_end < selection_end) {
                const int x = text_x + line_width(glyphs) - origin;
                if (x >= text_x && x < frame.right) {
                    screen.CellAt(x, y).background_color = frame.theme.selection;
                }
            }

            if (on_cursor_line) {
                paint_cursor(frame, y, glyphs, first, end, origin, last_row, content_end);
                // the cursor's row: underlined, or shaded (View tab).
                if (settings.underline_active_row) {
                    for (int x = text_x; x < frame.right; ++x) {
                        mark_active_row_cell(screen, x, y, text_x, settings.active_row_mark, frame.theme.shade);
                    }
                }
            }
        }
    }
    paint_scroll(frame, box, bar_x);
}

// --- mouse ---------------------------------------------------------------

std::size_t EditorView::byte_at(int x, int y) const {
    if (drawn_.empty()) {
        return document() ? document()->cursor() : 0;
    }
    if (y > drawn_.back().y) {
        return drawn_.back().end_byte;  // below the text: its end
    }
    const DrawnRow& row = drawn_[static_cast<std::size_t>(std::clamp(y - drawn_.front().y, 0, static_cast<int>(drawn_.size()) - 1))];
    for (std::size_t i = 0; i < row.cells.size(); ++i) {
        if (x < row.cells[i].second + row.widths[i]) {
            return row.cells[i].first;
        }
    }
    return row.end_byte;
}

bool EditorView::on_mouse(ftxui::Event event) {
    Document* document = this->document();
    if (!document || document->hidden_as_binary()) {
        return false;
    }
    const ftxui::Mouse& mouse = event.mouse();
    const bool inside = box_.Contain(mouse.x, mouse.y);

    if (mouse.button == ftxui::Mouse::WheelUp || mouse.button == ftxui::Mouse::WheelDown) {
        if (!inside) {
            return false;
        }
        wheel(mouse.button == ftxui::Mouse::WheelUp ? -get_mouse_sensitivity() : get_mouse_sensitivity());
        return true;
    }
    if ((mouse.button == ftxui::Mouse::WheelLeft || mouse.button == ftxui::Mouse::WheelRight) && inside) {
        if (!settings.word_wrap && settings.scroll_style == SCROLL_FREELY) {
            const ViewScope scope(*document, window_);
            const int step = get_mouse_sensitivity();
            document->view.left = std::max(0, document->view.left + (mouse.button == ftxui::Mouse::WheelLeft ? -step : step));
        }
        return true;
    }

    if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
        if (!inside) {
            return false;
        }
        workspace.focus_window(window_);  // before its cursor is touched: the click is this window's
        TakeFocus();
        visual_ = false;
        const std::size_t byte = byte_at(mouse.x, mouse.y);
        if (mouse.shift) {
            document->set_cursor(byte, true);  // Shift+click: select up to here
        } else {
            document->set_cursor(byte);
        }
        drag_anchor_ = document->anchor().value_or(byte);
        dragging_ = true;
        captured_mouse_ = CaptureMouse(event);
        return true;
    }

    if (dragging_ && mouse.motion == ftxui::Mouse::Moved) {
        // dragging above or below the text scrolls it, one row per move.
        if (mouse.y < box_.y_min || mouse.y > box_.y_max) {
            const int rows = mouse.y < box_.y_min ? -1 : 1;
            const RowPosition at = move_rows(*document, row_of(*document, document->cursor()), rows);
            const int column = std::max(0, mouse.x - text_x_) + (settings.word_wrap ? 0 : document->view.left);
            document->select(drag_anchor_, byte_in_row(*document, at, column));
        } else {
            document->select(drag_anchor_, byte_at(mouse.x, mouse.y));
        }
        return true;
    }

    if (dragging_ && mouse.motion == ftxui::Mouse::Released) {
        dragging_ = false;
        captured_mouse_.reset();
        if (!document->has_selection()) {
            document->clear_selection();
        }
        return true;
    }
    return false;
}

// --- keys ----------------------------------------------------------------

bool EditorView::OnEvent(ftxui::Event event) {
    if (event.is_mouse()) {
        return on_mouse(event);
    }
    Document* document = this->document();
    if (!document || document->hidden_as_binary()) {
        return false;
    }
    if (line_cache_.size() > 512) {
        line_cache_.clear();
    }
    if (visual_ && !document->anchor()) {
        visual_ = false;  // the selection was cleared (another file, an edit, ...)
    }
    return on_key(*document, event);
}

bool EditorView::on_key(Document& document, const ftxui::Event& event) {
    // selection and clipboard, in both modes (Ctrl+C is in ui/key_listener.cpp).
    if (event == Event::CtrlA) {
        document.select(0, document.text().size());
        return true;
    }
    if (event == Event::CtrlX) {
        cut_selection(document);
        return true;
    }
    if (event == Event::CtrlV) {
        paste(document);
        return true;
    }

    // moves, in both modes; with Shift (or in Normal mode's "v") they select.
    struct Key {
        const Event& event;
        Motion motion;
        bool shift;
    };
    const Key keys[] = {
        {Event::ArrowLeft, Motion::Left, false},          {shift_left, Motion::Left, true},
        {Event::ArrowRight, Motion::Right, false},        {shift_right, Motion::Right, true},
        {Event::ArrowUp, Motion::Up, false},              {shift_up, Motion::Up, true},
        {Event::ArrowDown, Motion::Down, false},          {shift_down, Motion::Down, true},
        {Event::Home, Motion::LineStart, false},          {shift_home, Motion::LineStart, true},
        {Event::End, Motion::LineEnd, false},             {shift_end, Motion::LineEnd, true},
        {Event::PageUp, Motion::PageUp, false},           {shift_page_up, Motion::PageUp, true},
        {Event::PageDown, Motion::PageDown, false},       {shift_page_down, Motion::PageDown, true},
        {ctrl_home, Motion::FileStart, false},            {ctrl_shift_home, Motion::FileStart, true},
        {ctrl_end, Motion::FileEnd, false},               {ctrl_shift_end, Motion::FileEnd, true},
        {Event::ArrowLeftCtrl, Motion::WordLeft, false},  {ctrl_shift_left, Motion::WordLeft, true},
        {Event::ArrowRightCtrl, Motion::WordRight, false}, {ctrl_shift_right, Motion::WordRight, true},
    };
    for (const Key& key : keys) {
        if (event == key.event) {
            // always consumed: at the edge of the text an arrow would
            // otherwise move the focus out of the editor.
            move(document, key.motion, key.shift || (mode == Mode::Normal && visual_));
            return true;
        }
    }

    return mode == Mode::Edit ? on_edit_key(document, event) : on_normal_key(document, event);
}

void EditorView::move(Document& document, Motion motion, bool extend) {
    const std::size_t cursor = document.cursor();
    const auto [selection_begin, selection_end] = document.selection();
    switch (motion) {
        case Motion::Left:
            // with a selection, Left / Right go to its start / end.
            document.set_cursor(document.has_selection() && !extend ? selection_begin : previous_glyph(document, cursor), extend);
            break;
        case Motion::Right:
            document.set_cursor(document.has_selection() && !extend ? selection_end : next_glyph(document, cursor), extend);
            break;
        case Motion::Up:
            move_vertically(document, -1, extend);
            break;
        case Motion::Down:
            move_vertically(document, 1, extend);
            break;
        case Motion::LineStart:
            document.set_cursor(document.line_start(document.line_of(cursor)), extend);
            break;
        case Motion::LineEnd:
            document.set_cursor(document.line_content_end(document.line_of(cursor)), extend);
            break;
        case Motion::PageUp:
        case Motion::PageDown: {
            const int rows = motion == Motion::PageUp ? -view_rows_ : view_rows_;
            if (settings.scroll_style == SCROLL_FREELY) {
                scroll_view(document, rows);  // the cursor keeps its place on screen
            }
            move_vertically(document, rows, extend);
            break;
        }
        case Motion::FileStart:
            document.set_cursor(0, extend);
            break;
        case Motion::FileEnd:
            document.set_cursor(document.text().size(), extend);
            break;
        case Motion::WordLeft:
            document.set_cursor(word_left(document.text(), cursor), extend);
            break;
        case Motion::WordRight:
            document.set_cursor(word_right(document.text(), cursor), extend);
            break;
    }
}

void EditorView::move_vertically(Document& document, int rows, bool extend) {
    // the column to come back to is kept while only Up / Down move the cursor.
    if (goal_document_ != document.id() || goal_cursor_ != document.cursor()) {
        goal_column_ = -1;
    }
    const RowPosition from = row_of(document, document.cursor());
    if (goal_column_ < 0) {
        const std::vector<std::size_t> row_starts = rows_of(document, from.line);
        const std::vector<GlyphCell>& glyphs = glyphs_of(document, from.line);
        const std::size_t first = row_starts[static_cast<std::size_t>(from.row)];
        const int origin = first < glyphs.size() ? glyphs[first].column : line_width(glyphs);
        goal_column_ = column_of(document, document.cursor()) - origin;
    }
    const RowPosition to = move_rows(document, from, rows);
    if (to.line == from.line && to.row == from.row) {
        // already on the first / last row: to the start / end of the text.
        document.set_cursor(rows < 0 ? 0 : document.text().size(), extend);
    } else {
        document.set_cursor(byte_in_row(document, to, goal_column_), extend);
    }
    goal_document_ = document.id();
    goal_cursor_ = document.cursor();
}

bool EditorView::on_edit_key(Document& document, const ftxui::Event& event) {
    if (event == Event::Return || event == ctrl_j) {
        insert_typed(document, document.line_break());
        return true;
    }
    // some terminals send Ctrl+H (0x08) for the Backspace key. FTXUI is
    // patched to keep Ctrl+H separate, so it is Backspace here.
    if (event == Event::Backspace || event == Event::CtrlH) {
        const auto [begin, end] = document.selection();
        if (begin != end) {
            document.replace(begin, end, "");
        } else if (document.cursor() > 0) {
            document.replace(previous_glyph(document, document.cursor()), document.cursor(), "");
        }
        return true;
    }
    if (event == Event::Delete) {
        const auto [begin, end] = document.selection();
        if (begin != end) {
            document.replace(begin, end, "");
        } else {
            document.replace(document.cursor(), next_glyph(document, document.cursor()), "");
        }
        return true;
    }
    // Tab / Shift+Tab indent and unindent. Without this the containers would
    // move the focus away instead.
    if (event == Event::Tab || event == Event::TabReverse) {
        indent(document, event == Event::Tab);
        return true;
    }
    if (event.is_character()) {
        insert_typed(document, event.character());
        return true;
    }
    return false;
}

bool EditorView::on_normal_key(Document& document, const ftxui::Event& event) {
    // h/j/k/l act as arrow keys.
    const std::pair<char, Motion> vim_moves[] = {
        {'h', Motion::Left}, {'j', Motion::Down}, {'k', Motion::Up}, {'l', Motion::Right}};
    for (const auto& [key, motion] : vim_moves) {
        if (event == Event::Character(key)) {
            move(document, motion, visual_);
            return true;
        }
    }

    // v starts (or ends) a selection, y copies it, d / x cut it, p pastes (like vim).
    if (event == Event::Character('v')) {
        visual_ = !visual_;
        if (visual_) {
            document.select(document.cursor(), document.cursor());
            command_output = "Selecting: move the cursor, then y copies, d cuts (v or Esc stops)";
        } else {
            document.clear_selection();
        }
        return true;
    }
    if (event == Event::Character('y')) {
        if (!copy_selection()) {
            command_error = "Nothing is selected: press v and move the cursor (or Shift + arrows)";
        }
        visual_ = false;
        document.clear_selection();
        return true;
    }
    if (event == Event::Character('d') || event == Event::Character('x')) {
        cut_selection(document);
        visual_ = false;
        return true;
    }
    if (event == Event::Character('p')) {
        paste(document);
        return true;
    }
    if (event == Event::Escape && (visual_ || document.has_selection())) {
        visual_ = false;
        document.clear_selection();
        return true;
    }

    // n / N repeat the last search, forward / backward (like vim).
    if (event == Event::Character('n') || event == Event::Character('N')) {
        const CommandResult result = search_again(event == Event::Character('N'));
        (result.ok ? command_output : command_error) = result.message;
        return true;
    }

    // u undoes, Ctrl+R redoes (like vim).
    if (event == Event::Character('u') || event == Event::CtrlR) {
        const CommandResult result = event == Event::CtrlR ? redo_edit() : undo_edit();
        (result.ok ? command_output : command_error) = result.message;
        return true;
    }

    // Enter Edit mode (e, or i like vim).
    if (event == Event::Character('e') || event == Event::Character('i')) {
        set_edit_mode();
        visual_ = false;
        TakeFocus();
        return true;
    }

    // printable characters and editing keys are consumed so Normal mode can
    // never change the text by accident.
    if (event.is_character() || event == Event::Return || event == Event::Backspace || event == Event::Delete ||
        event == ctrl_j) {
        return true;
    }
    return false;
}

// --- editing -------------------------------------------------------------

void EditorView::insert_typed(Document& document, std::string_view text) {
    const auto [begin, end] = document.selection();
    document.replace(begin, end, text);
}

void EditorView::insert_pasted(std::string_view text) {
    Document* document = this->document();
    if (!document || document->hidden_as_binary() || text.empty()) {
        return;
    }
    const auto [begin, end] = document->selection();
    document->replace(begin, end, with_line_breaks(text, document->line_break()), false);
    visual_ = false;
}

void EditorView::indent(Document& document, bool add) {
    const int tab = get_tab_size();
    const auto [begin, end] = document.selection();
    const std::size_t first_line = document.line_of(begin);
    const std::size_t last_line = document.line_of(end > begin ? end - 1 : end);

    // Tab inside one line: spaces up to the next tab stop (or a tab character).
    if (add && first_line == last_line) {
        const int column = column_of(document, begin);
        insert_typed(document, settings.tab_inserts_spaces ? std::string(static_cast<std::size_t>(tab - column % tab), ' ') : std::string("\t"));
        return;
    }

    // every line of the selection (or the cursor's line): one level more or less.
    const std::string& text = document.text();
    const std::size_t region_begin = document.line_start(first_line);
    const std::size_t region_end = document.line_content_end(last_line);
    const std::string unit = settings.tab_inserts_spaces ? std::string(static_cast<std::size_t>(tab), ' ') : std::string("\t");
    std::string region;
    std::size_t removed_on_cursor_line = 0;
    const std::size_t cursor_line = document.line_of(document.cursor());
    for (std::size_t line = first_line; line <= last_line; ++line) {
        const std::size_t start = document.line_start(line);
        const std::size_t stop = line == last_line ? region_end : document.line_start(line + 1);
        std::string_view part(text.data() + start, stop - start);
        if (add) {
            region += unit;
        } else {
            std::size_t count = 0;
            if (!part.empty() && part[0] == '\t') {
                count = 1;
            } else {
                while (count < static_cast<std::size_t>(tab) && count < part.size() && part[count] == ' ') {
                    ++count;
                }
            }
            part.remove_prefix(count);
            if (line == cursor_line) {
                removed_on_cursor_line = count;
            }
        }
        region += part;
    }
    if (region == std::string_view(text).substr(region_begin, region_end - region_begin)) {
        return;  // nothing to unindent
    }
    const bool had_selection = document.has_selection();
    const std::size_t cursor = document.cursor();
    const std::size_t cursor_line_start = document.line_start(cursor_line);
    document.replace(region_begin, region_end, region);
    if (had_selection) {
        document.select(region_begin, region_begin + region.size());  // the lines stay selected
    } else {
        document.set_cursor(std::max(cursor_line_start, cursor - std::min(cursor - cursor_line_start, removed_on_cursor_line)));
    }
}

bool EditorView::copy_selection() {
    Document* document = this->document();
    if (!document || !document->has_selection()) {
        return false;
    }
    const std::string text = document->selected_text();
    const std::string where = clipboard_copy(text);
    command_output = "Copied " + std::to_string(count_characters(text)) + " characters to " + where;
    return true;
}

void EditorView::cut_selection(Document& document) {
    if (!document.has_selection()) {
        command_error = "Nothing is selected: Shift + arrows, a mouse drag or v select text";
        return;
    }
    const std::string text = document.selected_text();
    const std::string where = clipboard_copy(text);
    const auto [begin, end] = document.selection();
    document.replace(begin, end, "", false);
    command_output = "Cut " + std::to_string(count_characters(text)) + " characters to " + where;
}

void EditorView::paste(Document& document) {
    bool from_system = false;
    const std::string text = clipboard_paste(from_system);
    if (text.empty()) {
        command_error = "The clipboard is empty";
        return;
    }
    const auto [begin, end] = document.selection();
    document.replace(begin, end, with_line_breaks(text, document.line_break()), false);
    visual_ = false;
    command_output = from_system ? "Pasted from the clipboard" : "Pasted the text last copied in Itahi";
}

void EditorView::cut() {
    if (Document* document = this->document()) {
        cut_selection(*document);
    }
}

void EditorView::copy() {
    if (!copy_selection()) {
        command_error = "Nothing is selected: Shift + arrows, a mouse drag or v select text";
    }
}

void EditorView::paste_clipboard() {
    if (Document* document = this->document(); document && !document->hidden_as_binary()) {
        paste(*document);
    }
}

void EditorView::select_all() {
    if (Document* document = this->document()) {
        document->select(0, document->text().size());
    }
}
