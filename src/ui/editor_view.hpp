#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/screen.hpp>

#include "ui/glyphs.hpp"

class Document;

// the text area of one window. It draws the window's document itself, only the
// rows on screen (with tabs expanded, syntax colors, search matches, the
// selection and the cursor's row marked), and handles the keys and the mouse:
// Itahi's modes, vim motions, editing, selection and the clipboard.
class EditorView : public ftxui::ComponentBase {
public:
    // the view of the window `window` (see Workspace): it shows that window's
    // document, with the window's own cursor, selection and scroll position.
    explicit EditorView(int window = 0) : window_(window) {}
    int window() const { return window_; }

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    // pasted text (bracketed paste): inserted at the cursor in one undo step,
    // replacing the selection.
    void insert_pasted(std::string_view text);
    // Ctrl+C: copies the selection; false when nothing is selected.
    bool copy_selection();
    // a mouse wheel notch over the editor: moves the view ("Scroll freely")
    // or the cursor ("Keep cursor centered") by `rows`.
    void wheel(int rows);

    // the Edit tab's buttons, on this window's document.
    void cut();
    void copy();
    void paste_clipboard();
    void select_all();

    // draws the view into `box` (called by the element OnRender() returns).
    void paint(ftxui::Screen& screen, const ftxui::Box& box, bool focused);

private:
    enum class Motion { Left, Right, Up, Down, LineStart, LineEnd, PageUp, PageDown, FileStart, FileEnd, WordLeft, WordRight };

    // a position in the view: a line, and (with word wrap) a row of it.
    struct RowPosition {
        std::size_t line;
        int row;
    };

    // one row of the last frame, for mouse clicks: its glyphs and where they are.
    struct DrawnRow {
        int y;
        std::vector<std::pair<std::size_t, int>> cells;  // (byte, first x) of every drawn glyph
        std::vector<int> widths;
        std::size_t end_byte;  // where a click past the last glyph goes
    };

    struct Frame;  // what one frame needs while it is painted

    Document* document() const;
    void paint_line_number(const Frame& frame, int y, std::size_t line, bool first_row);
    void paint_glyphs(const Frame& frame, int y, const std::vector<GlyphCell>& glyphs, std::size_t first, std::size_t end,
                      int origin, DrawnRow& drawn);
    void paint_cursor(const Frame& frame, int y, const std::vector<GlyphCell>& glyphs, std::size_t first, std::size_t end,
                      int origin, bool last_row, std::size_t content_end);
    void paint_scroll(const Frame& frame, const ftxui::Box& box, int bar_x);

    bool on_mouse(ftxui::Event event);
    bool on_key(Document& document, const ftxui::Event& event);
    bool on_edit_key(Document& document, const ftxui::Event& event);
    bool on_normal_key(Document& document, const ftxui::Event& event);
    void move(Document& document, Motion motion, bool extend);

    // the line's glyphs, kept until the text or the tab size changes. Don't
    // keep the reference across an edit.
    const std::vector<GlyphCell>& glyphs_of(const Document& document, std::size_t line);
    // the first glyph of every row of the line ({0} without word wrap).
    std::vector<std::size_t> rows_of(const Document& document, std::size_t line);
    int row_count(const Document& document, std::size_t line);
    int column_of(const Document& document, std::size_t byte);
    RowPosition row_of(const Document& document, std::size_t byte);
    // the glyph at `column` of a row (counted from the row's start), or its end.
    std::size_t byte_in_row(const Document& document, RowPosition row, int column);
    RowPosition move_rows(const Document& document, RowPosition from, int rows);
    void move_vertically(Document& document, int rows, bool extend);
    std::size_t previous_glyph(const Document& document, std::size_t from);
    std::size_t next_glyph(const Document& document, std::size_t from);
    std::size_t byte_at(int x, int y) const;

    void scroll_into_view(Document& document, bool cursor_moved);
    void scroll_view(Document& document, int rows);
    void insert_typed(Document& document, std::string_view text);
    void indent(Document& document, bool add);
    void cut_selection(Document& document);
    void paste(Document& document);

    int window_;

    // drawn in the last frame
    std::vector<DrawnRow> drawn_;
    ftxui::Box box_;
    int text_x_ = 0;
    int view_rows_ = 20;
    int text_columns_ = 80;
    int wrap_width_ = 79;

    // the cursor as seen by the last frame: when it moved, the view follows it.
    std::uint64_t seen_document_ = 0;  // Document::id()
    std::size_t seen_cursor_ = 0;
    std::uint64_t seen_version_ = 0;

    // Up / Down keep the column they started from (a "sticky" column).
    int goal_column_ = -1;
    std::uint64_t goal_document_ = 0;
    std::size_t goal_cursor_ = 0;

    // Normal mode "v": the cursor moves extend the selection.
    bool visual_ = false;

    // mouse drag selection
    bool dragging_ = false;
    std::size_t drag_anchor_ = 0;
    ftxui::CapturedMouse captured_mouse_;

    // the glyphs of the lines laid out lately (a deque: references to them
    // stay valid while more are added).
    std::uint64_t cache_document_ = 0;
    std::uint64_t cache_version_ = UINT64_MAX;
    int cache_tab_ = 0;
    std::deque<std::pair<std::size_t, std::vector<GlyphCell>>> line_cache_;
};

std::shared_ptr<EditorView> make_editor_view(int window = 0);
