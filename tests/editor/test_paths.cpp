#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "editor/commands.hpp"
#include "editor/paths.hpp"
#include "editor/state.hpp"
#include "test_helpers.hpp"

namespace fs = std::filesystem;
using test::TempDir;

TEST_CASE("paths typed in commands") {
    workspace_root = "./project";
    CHECK(resolve_user_path("/test/newFile.txt") == fs::path("./project/test/newFile.txt"));
    CHECK(resolve_user_path("src/a.cpp") == fs::path("src/a.cpp"));
    workspace_root = ".";
}

TEST_CASE(">e creates missing folders and the file") {
    TempDir dir;
    workspace_root = dir.path.string();
    long_active_file_name.clear();

    const CommandResult result = run_command("e /test/deeper/newFile.txt");
    CHECK(result.ok);
    CHECK(fs::is_regular_file(dir.path / "test/deeper/newFile.txt"));
    CHECK(long_active_file_name == (dir.path / "test/deeper/newFile.txt").string());
    CHECK(opened_folders.count((dir.path / "test").string()) == 1);          // revealed in the explorer
    CHECK(opened_folders.count((dir.path / "test/deeper").string()) == 1);

    CHECK_FALSE(run_command("e /test/").ok);   // a folder, no file name
    workspace_root = ".";
}

TEST_CASE("Tab completion, like cd") {
    TempDir dir;
    workspace_root = dir.path.string();
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
    workspace_root = ".";
}
