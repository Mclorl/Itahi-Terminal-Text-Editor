#include <doctest/doctest.h>

#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/screen.hpp>

#include "editor/mode.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "ui/editor_view.hpp"
#include "ui/glyphs.hpp"

namespace {

    std::vector<GlyphCell> layout(const std::string& text, int tab = 4) {
        std::vector<GlyphCell> glyphs;
        layout_line(text, 0, text.size(), tab, glyphs);
        return glyphs;
    }

    // the characters drawn on row `y`, from column `x` on.
    std::string row_text(ftxui::Screen& screen, int y, int x = 0) {
        std::string out;
        for (int i = x; i < screen.dimx(); ++i) {
            const std::string& c = screen.CellAt(i, y).character;
            out += c.empty() ? " " : c;  // FTXUI prints an empty cell as a space
        }
        return out;
    }

}  // namespace

TEST_CASE("tabs reach the next tab stop, wide characters take two columns") {
    const auto tabs = layout("a\tb\tc", 4);
    REQUIRE(tabs.size() == 5);
    CHECK(tabs[1].column == 1);
    CHECK(tabs[1].width == 3);                 // from column 1 to the stop at 4
    CHECK(tabs[2].column == 4);
    CHECK(tabs[3].width == 3);
    CHECK(line_width(tabs) == 9);

    const auto wide = layout("\xE6\x97\xA5\xE6\x9C\xAC!");   // two full-width characters and "!"
    REQUIRE(wide.size() == 3);
    CHECK(wide[0].width == 2);
    CHECK(wide[1].column == 2);
    CHECK(wide[2].column == 4);

    const auto combining = layout("e\xCC\x81x");             // "e" + combining acute accent, "x"
    REQUIRE(combining.size() == 2);
    CHECK(combining[0].size == 3);
    CHECK(combining[1].column == 1);

    const auto control = layout("a\x1B[1mb");                // ESC is never drawn
    CHECK(control[1].width == 0);

    const auto invalid = layout("\xFF");
    REQUIRE(invalid.size() == 1);
    CHECK(invalid[0].invalid);
}

TEST_CASE("word wrap breaks after spaces") {
    const std::string text = "aaa bbb ccc";
    const auto glyphs = layout(text);
    const auto rows = wrap_rows(glyphs, text, 5);
    CHECK(rows == std::vector<std::size_t>{0, 4, 8});
    CHECK(wrap_rows(layout(""), "", 5) == std::vector<std::size_t>{0});
}

TEST_CASE("the editor draws tabs, only the rows on screen, and edits") {
    workspace.clear();
    const Settings saved = settings;
    settings = Settings{};
    settings.syntax_highlighting = false;
    std::string text = "x\ty\n";
    for (int i = 2; i <= 1000; ++i) {
        text += "line " + std::to_string(i) + "\n";
    }
    Document& doc = workspace.add("view.txt", text);
    auto view = make_editor_view();

    ftxui::Screen screen(30, 5);
    view->paint(screen, {0, 29, 0, 4}, true);
    CHECK(row_text(screen, 0).rfind("   1 x   y", 0) == 0);  // the tab is shown, to column 4
    CHECK(row_text(screen, 4).rfind("   5 line 5", 0) == 0);  // five rows: lines 1 to 5

    // Ctrl+End: the view follows the cursor to the last line.
    view->OnEvent(ftxui::Event::Special("\x1B[1;5F"));
    ftxui::Screen bottom(30, 5);
    view->paint(bottom, {0, 29, 0, 4}, true);
    CHECK(row_text(bottom, 4).rfind("1001", 0) == 0);

    // typing in Edit mode, selecting with Shift, replacing the selection.
    doc.set_cursor(0);
    set_edit_mode();
    view->OnEvent(ftxui::Event::Character('A'));
    CHECK(doc.text().rfind("Ax\ty\n", 0) == 0);
    view->OnEvent(ftxui::Event::Special("\x1B[1;2C"));     // Shift+Right
    view->OnEvent(ftxui::Event::Special("\x1B[1;2C"));
    CHECK(doc.selected_text() == "x\t");
    view->OnEvent(ftxui::Event::Character('-'));
    CHECK(doc.text().rfind("A-y\n", 0) == 0);

    // Down keeps the column, even through a shorter line.
    doc.set_cursor(doc.line_start(1) + 6);                  // "line 2" after "line 2"[6]
    view->OnEvent(ftxui::Event::ArrowUp);
    CHECK(doc.cursor() == 3);                               // line 1 is shorter: its end
    view->OnEvent(ftxui::Event::ArrowDown);
    CHECK(doc.cursor() == doc.line_start(1) + 6);           // back to the same column

    // Tab inserts spaces up to the next stop; Shift+Tab takes them away.
    doc.set_cursor(doc.line_start(2));
    view->OnEvent(ftxui::Event::Tab);
    CHECK(doc.text().substr(doc.line_start(2), 8) == "    line");
    view->OnEvent(ftxui::Event::TabReverse);
    CHECK(doc.text().substr(doc.line_start(2), 6) == "line 3");

    set_normal_mode();
    view->OnEvent(ftxui::Event::Character('x'));            // Normal mode never types
    CHECK(doc.text().rfind("A-y\n", 0) == 0);
    settings = saved;
    workspace.clear();
}
