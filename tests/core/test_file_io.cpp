#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>

#include "core/file_io.hpp"
#include "core/trash.hpp"
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
    CHECK_FALSE(disk_stamp(file).exists);
    std::ofstream(file) << "one";
    const DiskStamp before = disk_stamp(file);
    CHECK(before.exists);
    CHECK(disk_stamp(file) == before);

    std::ofstream(file) << "something longer";
    CHECK(disk_stamp(file) != before);
    fs::remove(file);
    CHECK(disk_stamp(file) != before);            // deleted is a change too
}

TEST_CASE("only regular files are read, whatever their size says") {
    TempDir dir;
    std::string error;
    CHECK_FALSE(read_text_file(dir.path, error).has_value());   // a folder
    CHECK(error.find("folder") != std::string::npos);

    // a pipe nobody writes to: opening it must not wait forever.
    const fs::path pipe = dir.path / "pipe";
    REQUIRE(::mkfifo(pipe.c_str(), 0600) == 0);
    error.clear();
    CHECK_FALSE(read_text_file(pipe, error).has_value());
    CHECK(error.find("not a regular file") != std::string::npos);

    // files that say they are empty but aren't (like the ones in /proc).
    if (fs::exists("/proc/self/status")) {
        const auto text = read_text_file("/proc/self/status", error);
        REQUIRE(text.has_value());
        CHECK_FALSE(text->empty());
    }
}

TEST_CASE("saving keeps hard links, and works in a folder that can't take new files") {
    if (::geteuid() == 0) {
        return;  // root can write anything
    }
    TempDir dir;
    const fs::path file = dir.path / "linked.txt";
    const fs::path other_name = dir.path / "other-name.txt";
    std::ofstream(file) << "old";
    fs::create_hard_link(file, other_name);
    CHECK(save_atomic(file, "new").ok);
    CHECK(read_all(other_name) == "new");         // still the same file

    const fs::path locked = dir.path / "locked";
    fs::create_directories(locked);
    std::ofstream(locked / "note.txt") << "before";
    ::chmod(locked.c_str(), 0555);                // no new files here
    CHECK(save_atomic(locked / "note.txt", "after").ok);
    CHECK(read_all(locked / "note.txt") == "after");
    CHECK_FALSE(save_atomic(locked / "new.txt", "x").ok);
    ::chmod(locked.c_str(), 0755);
}

TEST_CASE("the trash keeps what is deleted, renaming never replaces") {
    TempDir dir;
    ::setenv("XDG_DATA_HOME", (dir.path / "data").c_str(), 1);
    const fs::path file = dir.path / "gone.txt";
    std::ofstream(file) << "keep me";
    fs::path trashed;
    REQUIRE(move_to_trash(file, &trashed).ok);
    CHECK_FALSE(fs::exists(file));
    CHECK(read_all(trashed) == "keep me");
    CHECK(read_all(dir.path / "data/Trash/info/gone.txt.trashinfo").find("Path=" + file.string()) != std::string::npos);

    std::ofstream(file) << "again";               // the same name twice
    REQUIRE(move_to_trash(file, &trashed).ok);
    CHECK(trashed.filename() == "gone.txt.2");
    ::unsetenv("XDG_DATA_HOME");

    std::ofstream(dir.path / "a") << "a";
    std::ofstream(dir.path / "b") << "b";
    CHECK_FALSE(rename_path(dir.path / "a", dir.path / "b").ok);
    CHECK(read_all(dir.path / "b") == "b");
    CHECK(rename_path(dir.path / "a", dir.path / "c").ok);
    CHECK(read_all(dir.path / "c") == "a");

    fs::create_directories(dir.path / "full");
    std::ofstream(dir.path / "full/x") << "x";
    CHECK_FALSE(delete_for_good(dir.path / "full").ok);   // not empty: refused
    CHECK(delete_for_good(dir.path / "full/x").ok);
    CHECK(delete_for_good(dir.path / "full").ok);
}
