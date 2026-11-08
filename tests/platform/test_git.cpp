#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "platform/git.hpp"
#include "platform/process.hpp"
#include "test_helpers.hpp"

using test::TempDir;

TEST_CASE("git status output is read into states per path") {
    const std::string porcelain = std::string("## main...origin/main [ahead 2, behind 1]") + '\0' +
                                  " M src/a.cpp" + '\0' + "?? notes.txt" + '\0' + "!! build/" + '\0' +
                                  "R  new_name.cpp" + '\0' + "old_name.cpp" + '\0' + "A  added.h" + '\0' +
                                  "UU conflict.c" + '\0';
    const GitStatus status = parse_git_status(porcelain, "/repo");
    CHECK(status.in_repository);
    CHECK(status.branch == "main");
    CHECK(status.ahead == 2);
    CHECK(status.behind == 1);
    CHECK(status.state_of("/repo/src/a.cpp") == 'M');
    CHECK(status.state_of("/repo/notes.txt") == '?');
    CHECK(status.state_of("/repo/new_name.cpp") == 'R');
    CHECK(status.state_of("/repo/old_name.cpp") == ' ');   // the old name of a rename isn't listed
    CHECK(status.state_of("/repo/added.h") == 'A');
    CHECK(status.state_of("/repo/conflict.c") == 'U');
    CHECK(status.ignored("/repo/build/main.o"));            // inside an ignored folder
    CHECK(status.changes_under("/repo/src"));
    CHECK_FALSE(status.changes_under("/repo/build"));
    CHECK(status.changed_count() == 5);

    const GitStatus fresh = parse_git_status(std::string("## No commits yet on trunk") + '\0', "/r");
    CHECK(fresh.branch == "trunk");
}

TEST_CASE("git in a real folder") {
    if (!program_exists("git")) {
        return;
    }
    TempDir dir;
    CHECK_FALSE(read_git_status(dir.path.string()).in_repository);  // not a repository

    REQUIRE(run_process({"git", "-C", dir.path.string(), "init", "-q"}).exit_code == 0);
    std::ofstream(dir.path / "hello.txt") << "hi\n";
    const GitStatus status = read_git_status(dir.path.string());
    CHECK(status.in_repository);
    CHECK_FALSE(status.branch.empty());
    CHECK(status.state_of((dir.path / "hello.txt").string()) == '?');
}

TEST_CASE("running programs: output, exit code, input, missing programs") {
    const ProcessResult echo = run_process({"cat"}, "piped in");
    CHECK(echo.started);
    CHECK(echo.exit_code == 0);
    CHECK(echo.output == "piped in");
    CHECK(run_process({"false"}).exit_code == 1);
    CHECK_FALSE(run_process({"no-such-program-itahi"}).started);
    CHECK(program_exists("sh"));
    CHECK_FALSE(program_exists("no-such-program-itahi"));
}
