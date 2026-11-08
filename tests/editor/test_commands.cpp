#include <doctest/doctest.h>

#include "editor/commands.hpp"
#include "editor/search.hpp"
#include "editor/state.hpp"
#include "editor/undo_history.hpp"
#include "editor/workspace.hpp"

TEST_CASE("command parsing rejects what it doesn't know") {
    CHECK_FALSE(run_command("qw").ok);
    CHECK_FALSE(run_command("w?").ok);
    CHECK_FALSE(run_command("e").ok);       // needs a file name
    CHECK_FALSE(run_command("q extra").ok);
    CHECK(run_command("   ").ok);           // empty: nothing to do
}

TEST_CASE("vim-like search, substitute, undo and redo commands") {
    workspace.clear();
    Document& doc = workspace.add("buffer.txt", "alpha beta\nbeta gamma\nalpha alpha\n");

    CHECK(run_command("/beta").ok);
    CHECK(doc.cursor() == 6);
    CHECK(search_again(false).ok);          // n
    CHECK(doc.cursor() == 11);
    CHECK(search_again(false).ok);          // wraps to the top
    CHECK(doc.cursor() == 6);
    CHECK(run_command("?alpha").ok);
    CHECK(doc.cursor() == 0);
    CHECK_FALSE(run_command("/nothing here").ok);

    CHECK(run_command("%s/alpha/omega/").ok);        // first match of each line
    CHECK(doc.text() == "omega beta\nbeta gamma\nomega alpha\n");
    CHECK(run_command("undo").ok);
    CHECK(doc.text() == "alpha beta\nbeta gamma\nalpha alpha\n");
    CHECK(run_command("redo").ok);
    CHECK(doc.text() == "omega beta\nbeta gamma\nomega alpha\n");
    CHECK(run_command("u").ok);

    doc.set_cursor(24);                              // on the third line
    CHECK(run_command("s/alpha/x/g").ok);
    CHECK(doc.text() == "alpha beta\nbeta gamma\nx x\n");
    CHECK_FALSE(run_command("s/zzz/y/").ok);
    CHECK(run_command("2").ok);                      // go to line 2
    CHECK(doc.cursor() == 11);
}

TEST_CASE("live search: highlight while typing, Enter walks through the matches") {
    workspace.clear();
    Document& doc = workspace.add("live.txt", "foo bar foo baz foo");

    search_preview("fo", false);                  // while typing: highlights only
    CHECK(search_state().matches.size() == 3);
    CHECK(doc.cursor() == 0);                      // the cursor doesn't move
    CHECK(search_match_kind(1) == 1);
    CHECK(search_match_kind(3) == 0);

    search_preview("foo b", false);               // exact text, spaces included
    CHECK(search_state().matches.size() == 2);

    search_preview("foo", false);
    CHECK(search_step().ok);                       // first Enter: the first match from the cursor
    CHECK(doc.cursor() == 0);
    CHECK(search_match_kind(0) == 2);              // the current match
    CHECK(search_step().message == "2 of 3");      // Enter again: the next one
    CHECK(doc.cursor() == 8);
    CHECK(search_step().ok);
    CHECK(doc.cursor() == 16);
    CHECK(search_step().ok);                       // wraps around to the top
    CHECK(doc.cursor() == 0);
    CHECK(search_step(true).ok);                   // Up: the previous one (wraps to the bottom)
    CHECK(doc.cursor() == 16);

    search_preview("x(y", false);                  // not a regular expression any more
    CHECK(search_state().matches.empty());
    CHECK_FALSE(search_step().ok);

    search_close();
    CHECK(search_match_kind(0) == 0);              // no highlights after Esc
}
