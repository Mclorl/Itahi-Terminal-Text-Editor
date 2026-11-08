#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "editor/commands.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "test_helpers.hpp"

namespace fs = std::filesystem;
using test::TempDir;
using test::read_all;

TEST_CASE("files open in tabs, read once, each with its own cursor") {
    TempDir dir;
    workspace.clear();
    workspace.root = dir.path.string();
    std::ofstream(dir.path / "a.txt") << "first";
    std::ofstream(dir.path / "b.txt") << "second";

    std::string error;
    Document* a = workspace.open(dir.path / "a.txt", error);
    REQUIRE(a);
    a->set_cursor(3);
    Document* b = workspace.open(dir.path / "b.txt", error);
    REQUIRE(b);
    CHECK(workspace.active() == b);
    CHECK(workspace.documents().size() == 2);

    CHECK(workspace.open(dir.path / "a.txt", error) == a);   // already open: shown again
    CHECK(a->cursor() == 3);

    Document* missing = workspace.open(dir.path / "new.txt", error);
    REQUIRE(missing);                                        // a new file: empty, not on disk yet
    CHECK(missing->text().empty());
    CHECK_FALSE(missing->exists_on_disk());
    CHECK_FALSE(fs::exists(dir.path / "new.txt"));
    workspace.root = ".";
}

TEST_CASE("switching and closing tabs") {
    workspace.clear();
    Document& one = workspace.add("one.txt", "1");
    Document& two = workspace.add("two.txt", "2");
    Document& three = workspace.add("three.txt", "3");
    CHECK(workspace.active() == &three);

    CHECK(run_command("bn").ok);                  // wraps around to the first
    CHECK(workspace.active() == &one);
    CHECK(run_command("bp").ok);
    CHECK(workspace.active() == &three);
    CHECK(run_command("b tw").ok);                // by the start of its name
    CHECK(workspace.active() == &two);
    CHECK_FALSE(run_command("b zzz").ok);

    two.replace(0, 0, "edited ");
    CHECK(workspace.first_unsaved() == &two);
    CHECK_FALSE(run_command("bd").ok);            // unsaved: refused
    CHECK(run_command("ls").message == "1 one.txt  [2 two.txt*]  3 three.txt");
    CHECK(run_command("bd!").ok);                 // closed without saving
    CHECK(workspace.documents().size() == 2);
    CHECK(workspace.active() == &three);          // the tab that took its place
    CHECK(workspace.first_unsaved() == nullptr);

    CHECK(run_command("bd").ok);
    CHECK(run_command("bd").ok);
    CHECK(workspace.active() == nullptr);
    CHECK_FALSE(run_command("bd").ok);            // nothing left to close
    CHECK_FALSE(run_command("bn").ok);
}

TEST_CASE("saving, and saving as another file") {
    TempDir dir;
    workspace.clear();
    workspace.root = dir.path.string();
    std::ofstream(dir.path / "draft.txt") << "v1";

    std::string error;
    Document* doc = workspace.open(dir.path / "draft.txt", error);
    REQUIRE(doc);
    doc->replace(2, 2, " and more");
    CHECK(doc->unsaved());
    CHECK(workspace.unsaved_under(dir.path.string()));
    CHECK(run_command("w").ok);
    CHECK_FALSE(doc->unsaved());
    CHECK(read_all(dir.path / "draft.txt") == "v1 and more");

    CHECK(run_command("w /copies/final.txt").ok);  // the buffer becomes the new file
    CHECK(read_all(dir.path / "copies/final.txt") == "v1 and more");
    CHECK(doc->path() == (fs::path(dir.path.string()) / "copies/final.txt").string());
    CHECK(workspace.documents().size() == 1);
    workspace.root = ".";
}
