#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "editor/commands.hpp"
#include "editor/recovery.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "test_helpers.hpp"

namespace fs = std::filesystem;
using test::TempDir;
using test::read_all;

namespace {

    using Layout = std::vector<std::vector<int>>;

    // the windows by number, the way they are on screen.
    Layout numbered() {
        return workspace.columns();
    }

}  // namespace

TEST_CASE("up to four windows: two columns of two") {
    workspace.clear();
    workspace.add("a.txt", "a");
    const int first = workspace.active_window();

    CHECK(run_command("vsplit").ok);                 // [1][2]
    const int second = workspace.active_window();
    CHECK(second != first);
    CHECK(numbered() == Layout{{first}, {second}});
    CHECK(workspace.document_of(second) == workspace.document_of(first));   // the same file, like vscode

    CHECK(run_command("split").ok);                  // [1][2 / 3]
    const int third = workspace.active_window();
    CHECK(numbered() == Layout{{first}, {second, third}});

    CHECK(run_command("vsplit").ok);                 // no third column: the room that is left
    const int fourth = workspace.active_window();
    CHECK(numbered() == Layout{{first, fourth}, {second, third}});
    CHECK(workspace.window_count() == 4);

    const CommandResult full = run_command("split");
    CHECK_FALSE(full.ok);                            // four is the most
    CHECK(workspace.window_count() == 4);

    CHECK(run_command("only").ok);
    CHECK(workspace.window_count() == 1);
    CHECK(workspace.active_window() == fourth);
    CHECK_FALSE(run_command("close").ok);            // the last window stays
}

TEST_CASE("going from window to window") {
    workspace.clear();
    workspace.add("a.txt", "a");
    const int a = workspace.active_window();
    std::string note;
    const int b = workspace.split(Side::Right, note);
    const int c = workspace.split(Side::Down, note);  // [a][b / c], c active

    CHECK(workspace.window_number(a) == 1);
    CHECK(workspace.window_number(b) == 2);
    CHECK(workspace.window_number(c) == 3);
    CHECK(workspace.focus_side(Side::Left));
    CHECK(workspace.active_window() == a);
    CHECK_FALSE(workspace.focus_side(Side::Left));    // nothing further left
    CHECK(workspace.focus_side(Side::Right));
    CHECK(workspace.active_window() == c);            // back to the row it came from
    CHECK(workspace.focus_side(Side::Up));
    CHECK(workspace.active_window() == b);
    CHECK(run_command("win 1").ok);
    CHECK(workspace.active_window() == a);
    CHECK(run_command("win prev").ok);
    CHECK(workspace.active_window() == c);
    CHECK_FALSE(run_command("win 4").ok);

    CHECK(run_command("close").ok);                   // c goes: b is next to it
    CHECK(workspace.active_window() == b);
    CHECK(workspace.documents().size() == 1);         // closing a window closes no file
}

TEST_CASE("moving a window to another place") {
    workspace.clear();
    workspace.add("a.txt", "a");
    const int a = workspace.active_window();
    std::string note, error;
    const int b = workspace.split(Side::Right, note);   // [a][b], b active

    CHECK(workspace.move_window(Side::Left, error));    // side by side: they change places
    CHECK(numbered() == Layout{{b}, {a}});
    CHECK(workspace.move_window(Side::Down, error));    // under the other one
    CHECK(numbered() == Layout{{a, b}});
    CHECK(workspace.move_window(Side::Up, error));
    CHECK(numbered() == Layout{{b, a}});
    CHECK_FALSE(workspace.move_window(Side::Up, error));  // already at the top
    CHECK(workspace.move_window(Side::Right, error));   // a column of its own again
    CHECK(numbered() == Layout{{a}, {b}});

    workspace.focus_window(a);
    const int c = workspace.split(Side::Down, note);    // [a / c][b], c active (2nd row)
    CHECK(workspace.move_window(Side::Right, error));   // to the 2nd column, 2nd row
    CHECK(numbered() == Layout{{a}, {b, c}});
    CHECK(workspace.move_window(Side::Up, error));      // and up to its 1st row
    CHECK(numbered() == Layout{{a}, {c, b}});
    CHECK(run_command("wmove left").ok);                // joins the 1st column, in its 1st row
    CHECK(numbered() == Layout{{c, a}, {b}});
    CHECK_FALSE(run_command("wmove sideways").ok);
}

TEST_CASE("two windows on one file each keep their place; a closed file leaves both") {
    workspace.clear();
    Document& one = workspace.add("one.txt", "first line\nsecond line\n");
    Document& two = workspace.add("two.txt", "other");
    workspace.activate(&one);
    one.set_cursor(3);
    const int left = workspace.active_window();
    std::string note;
    const int right = workspace.split(Side::Right, note);
    one.set_cursor(15);                               // the right window's cursor
    workspace.focus_window(left);
    CHECK(one.cursor() == 3);                         // the left one's is where it was
    workspace.focus_window(right);
    CHECK(one.cursor() == 15);

    CHECK(run_command("bd").ok);                      // the file both windows show
    CHECK(workspace.document_of(left) == &two);
    CHECK(workspace.document_of(right) == &two);
    workspace.clear();
}

TEST_CASE("explorer commands: mkdir, rename, rm") {
    TempDir dir;
    ::setenv("XDG_DATA_HOME", (dir.path / "data").c_str(), 1);
    set_recovery_folder(dir.path / "recovery");
    workspace.clear();
    workspace.root = dir.path.string();

    CHECK(run_command("mkdir /notes/2026").ok);
    CHECK(fs::is_directory(dir.path / "notes/2026"));
    CHECK_FALSE(run_command("mkdir /notes").ok);      // already there

    CHECK(run_command("e /notes/draft.txt").ok);
    Document* doc = workspace.active();
    REQUIRE(doc);
    doc->replace(0, 0, "text");
    CHECK(run_command("rename final.txt").ok);        // the open file, with its unsaved text
    CHECK(fs::exists(dir.path / "notes/final.txt"));
    CHECK_FALSE(fs::exists(dir.path / "notes/draft.txt"));
    CHECK(doc->path() == (dir.path / "notes/final.txt").string());
    CHECK(doc->text() == "text");

    std::ofstream(dir.path / "notes/taken.txt") << "x";
    CHECK_FALSE(run_command("rename taken.txt").ok);  // never over another file
    CHECK_FALSE(run_command("rm").ok);                // unsaved changes: refused
    CHECK(run_command("w").ok);
    CHECK(run_command("rm").ok);                      // to the trash, its tab closed
    CHECK_FALSE(fs::exists(dir.path / "notes/final.txt"));
    CHECK(read_all(dir.path / "data/Trash/files/final.txt") == "text");
    CHECK(workspace.active() == nullptr);

    CHECK_FALSE(run_command("rm! /notes").ok);        // for good: only an empty folder
    CHECK(run_command("rm /notes").ok);               // the trash takes it whole
    CHECK_FALSE(run_command("rm /").ok);              // never the opened folder
    ::unsetenv("XDG_DATA_HOME");
    set_recovery_folder({});
    workspace.root = ".";
    workspace.clear();
}

TEST_CASE("a file changed by another program is read again, unless it has unsaved changes") {
    TempDir dir;
    set_recovery_folder(dir.path / "recovery");
    workspace.clear();
    workspace.root = dir.path.string();
    const fs::path file = dir.path / "shared.txt";
    std::ofstream(file) << "version one";
    std::string error;
    Document* doc = workspace.open(file, error);
    REQUIRE(doc);
    CHECK(check_files_on_disk().empty());

    std::ofstream(file) << "version two, longer";
    CHECK_FALSE(check_files_on_disk().empty());
    CHECK(doc->text() == "version two, longer");
    CHECK_FALSE(doc->unsaved());

    doc->replace(0, 0, "mine ");
    std::ofstream(file) << "version three, even longer";
    CHECK(check_files_on_disk().find("changed on disk") != std::string::npos);
    CHECK(doc->text() == "mine version two, longer");   // untouched
    CHECK(check_files_on_disk().empty());               // said once
    CHECK_FALSE(run_command("w").ok);                   // and not overwritten by accident
    CHECK_FALSE(run_command("e").ok);
    CHECK(run_command("e!").ok);
    CHECK(doc->text() == "version three, even longer");
    set_recovery_folder({});
    workspace.root = ".";
    workspace.clear();
}

TEST_CASE("unsaved changes are kept for recovery and brought back") {
    TempDir dir;
    set_recovery_folder(dir.path / "state/recovery");
    workspace.clear();
    workspace.root = dir.path.string();
    const fs::path file = dir.path / "work.txt";
    std::ofstream(file) << "saved text";
    std::string error;
    Document* doc = workspace.open(file, error);
    REQUIRE(doc);
    doc->replace(0, 0, "unsaved ");
    CHECK(update_recovery_copies(std::chrono::milliseconds(0)).empty());
    REQUIRE(recovery_copies().size() == 1);
    CHECK(recovery_copies()[0].path == file.string());
    CHECK((fs::status(recovery_copies()[0].file).permissions() & fs::perms::others_read) == fs::perms::none);

    // the editor is gone (killed): a new one finds the copy.
    workspace.clear();
    const CommandResult back = run_command("recover");
    CHECK(back.ok);
    REQUIRE(workspace.active());
    CHECK(workspace.active()->text() == "unsaved saved text");
    CHECK(workspace.active()->unsaved());
    CHECK(workspace.active()->undo());                  // what is on disk is one step away
    CHECK(workspace.active()->text() == "saved text");
    CHECK(workspace.active()->redo());

    CHECK(run_command("w").ok);                         // saved: the copy is gone
    CHECK(recovery_copies().empty());
    CHECK(read_all(file) == "unsaved saved text");

    workspace.active()->replace(0, 0, "x");
    CHECK(keep_all_recovery_copies() == 1);
    CHECK(run_command("recover!").ok);                  // forgotten on purpose
    CHECK(recovery_copies().empty());
    set_recovery_folder({});
    workspace.root = ".";
    workspace.clear();
}
