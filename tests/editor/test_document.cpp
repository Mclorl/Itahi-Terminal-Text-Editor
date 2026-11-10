#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "editor/document.hpp"
#include "editor/motion.hpp"

TEST_CASE("a document knows when it differs from the file on disk") {
    Document doc("notes.txt", "hello", true);
    CHECK_FALSE(doc.unsaved());
    CHECK(doc.name() == "notes.txt");

    doc.replace(5, 5, " world");
    CHECK(doc.text() == "hello world");
    CHECK(doc.cursor() == 11);               // after what was inserted
    CHECK(doc.unsaved());

    doc.replace(5, 11, "");                  // typed back to what is on disk
    CHECK_FALSE(doc.unsaved());

    doc.replace(0, 0, "> ");
    doc.mark_saved();
    CHECK_FALSE(doc.unsaved());
    doc.replace(0, 2, "", false);
    CHECK(doc.unsaved());
    CHECK(doc.undo());                       // back to what was saved
    CHECK_FALSE(doc.unsaved());
}

TEST_CASE("lines, with \\n and \\r\\n line breaks") {
    Document doc("a.txt", "one\ntwo\r\nthree", true);
    CHECK(doc.line_count() == 3);
    CHECK(doc.line_of(0) == 0);
    CHECK(doc.line_of(4) == 1);
    CHECK(doc.line_of(9) == 2);
    CHECK(doc.line_start(2) == 9);
    CHECK(doc.line_content_end(0) == 3);     // at the '\n'
    CHECK(doc.line_content_end(1) == 7);     // at the '\r' of "\r\n"
    CHECK(doc.line_content_end(2) == 14);    // the end of the text
    CHECK(doc.line_break() == "\n");         // the first line's

    Document windows("b.txt", "a\r\nb\r\n", true);
    CHECK(windows.line_break() == "\r\n");

    const auto version = doc.version();
    doc.replace(0, 0, "zero\n");
    CHECK(doc.version() != version);         // caches see the change
    CHECK(doc.line_count() == 4);
}

TEST_CASE("typing is one undo step, a command or a paste is its own") {
    Document doc("u.txt", "", false);
    doc.replace(0, 0, "a");
    doc.replace(1, 1, "b");                  // typed right after: same step
    doc.replace(2, 2, "PASTE", false);       // its own step
    doc.replace(7, 7, "c");                  // typing after a paste: a new step
    CHECK(doc.text() == "abPASTEc");

    CHECK(doc.undo());
    CHECK(doc.text() == "abPASTE");
    CHECK(doc.undo());
    CHECK(doc.text() == "ab");
    CHECK(doc.cursor() == 2);
    CHECK(doc.undo());
    CHECK(doc.text().empty());
    CHECK_FALSE(doc.undo());                 // nothing left

    CHECK(doc.redo());
    CHECK(doc.text() == "ab");
    doc.replace(2, 2, "!", false);           // a new edit drops the redo steps
    CHECK_FALSE(doc.redo());
}

TEST_CASE("the selection follows the cursor while extending") {
    Document doc("s.txt", "hello world", true);
    doc.set_cursor(6);
    doc.set_cursor(11, true);                // Shift+End
    CHECK(doc.has_selection());
    CHECK(doc.selected_text() == "world");
    CHECK(doc.selection() == std::pair<std::size_t, std::size_t>{6, 11});

    doc.set_cursor(3, true);                 // back past the anchor
    CHECK(doc.selected_text() == "lo ");

    const auto [begin, end] = doc.selection();
    doc.replace(begin, end, "");             // typing over it
    CHECK(doc.text() == "helworld");
    CHECK_FALSE(doc.has_selection());

    doc.select(0, 3);
    CHECK(doc.selected_text() == "hel");
    doc.set_cursor(5);                       // a move without Shift
    CHECK_FALSE(doc.has_selection());
}

TEST_CASE("binary files stay hidden until opened anyway") {
    Document binary("prog", std::string("\x7f" "ELF\0\0", 6), true);
    CHECK(binary.binary());
    CHECK(binary.hidden_as_binary());
    binary.open_anyway();
    CHECK_FALSE(binary.hidden_as_binary());

    Document text("t.txt", "plain", true);
    CHECK_FALSE(text.hidden_as_binary());
}

TEST_CASE("word and character moves") {
    const std::string text = "int main(void) {\n    return 0;";
    CHECK(word_right(text, 0) == 3);         // end of "int"
    CHECK(word_right(text, 3) == 8);         // end of "main"
    CHECK(word_right(text, 8) == 9);         // "(" is a word of punctuation
    CHECK(word_left(text, 8) == 4);          // start of "main"
    CHECK(word_left(text, 24) == 21);        // inside "return": its start
    CHECK(word_left(text, 21) == 15);        // from there, over the indent and the line break, to "{"
    CHECK(word_left(text, 0) == 0);
    CHECK(word_right(text, text.size()) == text.size());

    const std::string utf8 = "a\xC3\xA9z";   // "aéz"
    CHECK(next_char(utf8, 1) == 3);          // over both bytes of "é"
    CHECK(previous_char(utf8, 3) == 1);
    CHECK(previous_char(utf8, 0) == 0);
}

TEST_CASE("undo keeps only what changed, within a budget") {
    std::string big(200000, 'x');
    Document doc("big.txt", big, true);
    for (int i = 0; i < 50; ++i) {
        doc.replace(static_cast<std::size_t>(i) * 10, static_cast<std::size_t>(i) * 10, "edit", false);
    }
    CHECK(doc.undo_steps() == 50);
    CHECK(doc.history_bytes() < 10000);      // not 50 copies of the text
    for (int i = 0; i < 50; ++i) {
        CHECK(doc.undo());
    }
    CHECK(doc.text() == big);
    CHECK_FALSE(doc.unsaved());

    // Backspace over what was just typed leaves nothing behind.
    Document typed("t.txt", "", false);
    typed.replace(0, 0, "a");
    typed.replace(1, 1, "b");
    typed.replace(1, 2, "");
    CHECK(typed.text() == "a");
    CHECK(typed.undo());
    CHECK(typed.text().empty());
    CHECK(typed.redo());
    CHECK(typed.text() == "a");
}

TEST_CASE("the line index follows every edit") {
    // against a count from scratch, after edits of every kind.
    Document doc("lines.txt", "one\ntwo\nthree\n", true);
    const auto check = [&doc] {
        std::vector<std::size_t> expected{0};
        for (std::size_t i = 0; i < doc.text().size(); ++i) {
            if (doc.text()[i] == '\n') {
                expected.push_back(i + 1);
            }
        }
        CHECK(doc.line_starts() == expected);
    };
    unsigned seed = 12345;
    const auto next = [&seed](std::size_t limit) {
        seed = seed * 1103515245u + 12345u;
        return static_cast<std::size_t>((seed >> 8) % (limit + 1));
    };
    const char* pieces[] = {"", "x", "\n", "ab\ncd", "\n\n", "\r\n", "long line without a break"};
    for (int i = 0; i < 400; ++i) {
        const std::size_t begin = next(doc.text().size());
        const std::size_t end = std::min(doc.text().size(), begin + next(6));
        doc.replace(begin, end, pieces[next(6)], i % 3 == 0);
        check();
        if (i % 7 == 0) {
            doc.undo();
            check();
        }
        if (i % 11 == 0) {
            doc.redo();
            check();
        }
    }
    while (doc.undo()) {
    }
    check();
}

TEST_CASE("every edit is reported with its position, for the syntax tree") {
    Document doc("e.txt", "ab\ncd\n", true);
    const std::uint64_t before = doc.version();
    doc.replace(4, 5, "X\nY", false);        // "ab\ncX\nY\n"
    std::vector<TextEdit> edits;
    REQUIRE(doc.edits_since(before, edits));
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].start == 4);
    CHECK(edits[0].old_end == 5);
    CHECK(edits[0].new_end == 7);
    CHECK(edits[0].start_point.row == 1);
    CHECK(edits[0].start_point.column == 1);
    CHECK(edits[0].new_end_point.row == 2);
    CHECK(edits[0].new_end_point.column == 1);
    CHECK(doc.edits_since(doc.version(), edits));
    CHECK(edits.empty());
    CHECK_FALSE(doc.edits_since(doc.version() + 1, edits));   // a version it never had
}

TEST_CASE("each window has its own cursor in a document, and follows the edits") {
    Document doc("v.txt", "line one\nline two\nline three\n", true);
    doc.set_cursor(20);                      // view 0, in "line three"
    doc.use_view(7);                         // a second window: starts where the first is
    CHECK(doc.cursor() == 20);
    doc.set_cursor(0);
    doc.replace(0, 0, "NEW\n");              // typed at the top, in view 7
    CHECK(doc.cursor() == 4);
    doc.use_view(0);
    CHECK(doc.cursor() == 24);               // still at the same place in "line three"
    doc.use_view(7);
    CHECK(doc.cursor() == 4);
    doc.drop_view(0);
    doc.use_view(0);                         // forgotten: starts where view 7 is
    CHECK(doc.cursor() == 4);
}

TEST_CASE("a file changed on disk is read again as one step that can be undone") {
    Document doc("r.txt", "alpha\nbeta\ngamma\n", true);
    doc.set_cursor(12);                      // in "gamma"
    doc.reload("alpha\nBETA and more\ngamma\n");
    CHECK(doc.text() == "alpha\nBETA and more\ngamma\n");
    CHECK_FALSE(doc.unsaved());
    CHECK(doc.cursor() == 21);               // still in "gamma"
    CHECK(doc.undo());
    CHECK(doc.text() == "alpha\nbeta\ngamma\n");
    CHECK(doc.unsaved());
}
