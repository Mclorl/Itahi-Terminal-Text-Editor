#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "editor/commands.hpp"
#include "editor/paths.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "test_helpers.hpp"

namespace fs = std::filesystem;
using test::TempDir;

TEST_CASE("paths typed in commands") {
    workspace.root = "./project";
    CHECK(resolve_user_path("/test/newFile.txt") == fs::path("./project/test/newFile.txt"));
    CHECK(resolve_user_path("src/a.cpp") == fs::path("src/a.cpp"));
    workspace.root = ".";
}

TEST_CASE(">e creates missing folders and the file") {
    TempDir dir;
    workspace.root = dir.path.string();
    workspace.clear();

    const CommandResult result = run_command("e /test/deeper/newFile.txt");
    CHECK(result.ok);
    CHECK(fs::is_regular_file(dir.path / "test/deeper/newFile.txt"));
    CHECK(active_path() == (dir.path / "test/deeper/newFile.txt").string());
    CHECK(workspace.opened_folders.count((dir.path / "test").string()) == 1);          // revealed in the explorer
    CHECK(workspace.opened_folders.count((dir.path / "test/deeper").string()) == 1);

    CHECK_FALSE(run_command("e /test/").ok);   // a folder, no file name
    workspace.root = ".";
}

TEST_CASE("Tab completion, like cd") {
    TempDir dir;
    workspace.root = dir.path.string();
    fs::create_directories(dir.path / "source");
    fs::create_directories(dir.path / "scripts");
    fs::create_directories(dir.path / "tests");
    fs::create_directories(dir.path / ".hidden");
    std::ofstream(dir.path / "setup.txt") << "x";

    PathCompletion one = complete_path("/te");
    CHECK(one.text == "/tests/");                // a single folder: completed, with '/'
    CHECK(one.options.empty());

    PathCompletion many = complete_path("/s");
    CHECK(many.text == "/s");                    // nothing more in common
    CHECK(many.options.size() == 3);             // scripts/ setup.txt source/

    PathCompletion common = complete_path("/sc");
    CHECK(common.text == "/scripts/");

    CHECK(complete_path("/").options.size() == 4);   // hidden ones left out
    CHECK(complete_path("/.h").text == "/.hidden/"); // unless asked for
    CHECK(complete_path("/zzz").text == "/zzz");     // nothing matches: unchanged
    workspace.root = ".";
}

TEST_CASE("the active folder is offered, a plain name goes to the opened folder") {
    TempDir dir;
    workspace.root = dir.path.string();
    fs::create_directories(dir.path / "docs");
    fs::create_directories(dir.path / "docs/drafts");

    // nothing selected: no folder to offer.
    hooks.active_folder = nullptr;
    CHECK(resolve_user_path("notes.txt") == dir.path / "notes.txt");

    // a folder selected in the explorer: offered as "docs/" (the ghost after
    // ">e", or written in by New File), but a plain name stays at the top.
    const std::string docs = (dir.path / "docs").string();
    hooks.active_folder = [docs] { return docs; };
    CHECK(active_folder_for_command() == (dir.path / "docs").lexically_normal().string() + "/");
    CHECK(resolve_user_path("top.txt") == dir.path / "top.txt");

    // a folder that was deleted by another program isn't offered: the
    // nearest one above it that is still there.
    const std::string gone = (dir.path / "docs/drafts/old/older").string();
    hooks.active_folder = [gone] { return gone; };
    CHECK(current_new_file_folder() == dir.path / "docs/drafts");

    const fs::path before = fs::current_path();
    fs::current_path(dir.path);
    workspace.root = ".";
    hooks.active_folder = [] { return std::string("./docs"); };
    CHECK(active_folder_for_command() == "docs/");  // no leading "./"
    hooks.active_folder = [] { return std::string("."); };
    CHECK(active_folder_for_command().empty());     // the start folder: nothing to offer

    // however a file is spelled, it is the explorer's "./docs/a.txt".
    CHECK(explorer_form("docs/a.txt") == fs::path("./docs/a.txt"));
    CHECK(explorer_form("docs/drafts/../a.txt") == fs::path("./docs/a.txt"));
    CHECK(explorer_form(fs::current_path() / "docs/a.txt") == fs::path("./docs/a.txt"));
    CHECK(explorer_form("/somewhere/else.txt") == fs::path("/somewhere/else.txt"));
    fs::current_path(before);

    hooks.active_folder = nullptr;
    workspace.root = ".";
}
