#include <doctest/doctest.h>

#include "editor/commands.hpp"
#include "editor/search.hpp"
#include "editor/state.hpp"
#include "editor/undo_history.hpp"

TEST_CASE("command parsing rejects what it doesn't know") {
    CHECK_FALSE(run_command("qw").ok);
    CHECK_FALSE(run_command("w?").ok);
    CHECK_FALSE(run_command("e").ok);       // needs a file name
    CHECK_FALSE(run_command("q extra").ok);
    CHECK(run_command("   ").ok);           // empty: nothing to do
}

TEST_CASE("vim-like search, substitute, undo and redo commands") {
    long_active_file_name = "buffer.txt";
    content = "alpha beta\nbeta gamma\nalpha alpha\n";
    content_read_first[long_active_file_name] = content;
    cursor_index = 0;

    CHECK(run_command("/beta").ok);
    CHECK(cursor_index == 6);
    CHECK(search_again(false).ok);          // n
    CHECK(cursor_index == 11);
    CHECK(search_again(false).ok);          // wraps to the top
    CHECK(cursor_index == 6);
    CHECK(run_command("?alpha").ok);
    CHECK(cursor_index == 0);
    CHECK_FALSE(run_command("/nothing here").ok);

    CHECK(run_command("%s/alpha/omega/").ok);        // first match of each line
    CHECK(content == "omega beta\nbeta gamma\nomega alpha\n");
    CHECK(run_command("undo").ok);
    CHECK(content == "alpha beta\nbeta gamma\nalpha alpha\n");
    CHECK(run_command("redo").ok);
    CHECK(content == "omega beta\nbeta gamma\nomega alpha\n");
    CHECK(run_command("u").ok);

    cursor_index = 24;                               // on the third line
    CHECK(run_command("s/alpha/x/g").ok);
    CHECK(content == "alpha beta\nbeta gamma\nx x\n");
    CHECK_FALSE(run_command("s/zzz/y/").ok);
    CHECK(run_command("2").ok);                      // go to line 2
    CHECK(cursor_index == 11);
}

TEST_CASE("live search: highlight while typing, Enter walks through the matches") {
    long_active_file_name = "live.txt";
    content = "foo bar foo baz foo";
    content_read_first[long_active_file_name] = content;
    cursor_index = 0;

    search_preview("fo", false);                  // while typing: highlights only
    CHECK(search_state().matches.size() == 3);
    CHECK(cursor_index == 0);                      // the cursor doesn't move
    CHECK(search_match_kind(1) == 1);
    CHECK(search_match_kind(3) == 0);

    search_preview("foo b", false);               // exact text, spaces included
    CHECK(search_state().matches.size() == 2);

    search_preview("foo", false);
    CHECK(search_step().ok);                       // first Enter: the first match from the cursor
    CHECK(cursor_index == 0);
    CHECK(search_match_kind(0) == 2);              // the current match
    CHECK(search_step().message == "2 of 3");      // Enter again: the next one
    CHECK(cursor_index == 8);
    CHECK(search_step().ok);
    CHECK(cursor_index == 16);
    CHECK(search_step().ok);                       // wraps around to the top
    CHECK(cursor_index == 0);
    CHECK(search_step(true).ok);                   // Up: the previous one (wraps to the bottom)
    CHECK(cursor_index == 16);

    search_preview("x(y", false);                  // not a regular expression any more
    CHECK(search_state().matches.empty());
    CHECK_FALSE(search_step().ok);

    search_close();
    CHECK(search_match_kind(0) == 0);              // no highlights after Esc
}
