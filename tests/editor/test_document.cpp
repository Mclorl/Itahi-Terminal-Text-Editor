#include <doctest/doctest.h>

#include <string>

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
    CHECK(doc.saved_text() == "> hello");
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
