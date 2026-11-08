#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include "core/file_io.hpp"
#include "test_helpers.hpp"

namespace fs = std::filesystem;
using test::TempDir;
using test::read_all;

TEST_CASE("folders are listed sorted, and errors don't throw") {
    TempDir dir;
    for (const char* name : {"zeta", "Beta", "alpha"}) {
        std::ofstream(dir.path / name) << "x";
    }
    std::string error;
    const auto entries = list_directory(dir.path, error);
    REQUIRE(entries.size() == 3);
    CHECK(error.empty());
    CHECK(entries[0].filename() == "alpha");
    CHECK(entries[1].filename() == "Beta");
    CHECK(entries[2].filename() == "zeta");

    const auto missing = list_directory(dir.path / "missing", error);
    CHECK(missing.empty());
    CHECK_FALSE(error.empty());
}

TEST_CASE("saving is atomic and keeps the file's mode") {
    TempDir dir;
    const fs::path file = dir.path / "script.sh";
    std::ofstream(file) << "old";
    ::chmod(file.c_str(), 0755);

    CHECK(save_atomic(file, "new contents").ok);
    CHECK(read_all(file) == "new contents");
    CHECK((fs::status(file).permissions() & fs::perms::owner_exec) != fs::perms::none);
    CHECK(std::distance(fs::directory_iterator(dir.path), fs::directory_iterator{}) == 1);  // no temp file left
}

TEST_CASE("a read-only file is refused, not replaced") {
    if (::geteuid() == 0) {
        return;  // root can write anything
    }
    TempDir dir;
    const fs::path file = dir.path / "ro.txt";
    std::ofstream(file) << "keep";
    ::chmod(file.c_str(), 0444);

    const IoResult result = save_atomic(file, "replaced");
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("Permission denied") != std::string::npos);
    CHECK(read_all(file) == "keep");
}

TEST_CASE("unreadable files are an error, not an empty file") {
    TempDir dir;
    std::string error;
    CHECK_FALSE(read_text_file(dir.path / "missing.txt", error).has_value());
    CHECK_FALSE(error.empty());

    std::ofstream(dir.path / "a.txt") << "hello";
    error.clear();
    const auto text = read_text_file(dir.path / "a.txt", error);
    REQUIRE(text.has_value());
    CHECK(*text == "hello");
}

TEST_CASE("changes on disk are noticed") {
    TempDir dir;
    const fs::path file = dir.path / "a.txt";
    std::ofstream(file) << "one";
    remember_disk_stamp(file.string());
    CHECK_FALSE(disk_changed_since_read(file.string()));

    std::ofstream(file) << "something longer";
    CHECK(disk_changed_since_read(file.string()));
}
