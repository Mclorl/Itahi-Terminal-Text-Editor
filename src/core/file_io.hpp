#pragma once

// files on disk: reading, atomic saving, listing folders, noticing outside
// changes. Standard library and POSIX only.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// result of a file operation: `error` says what went wrong when not `ok`.
struct IoResult {
    bool ok;
    std::string error;
};

// the largest file the editor opens. Past this a file is refused with a
// message instead of the editor running out of memory with unsaved work open.
inline constexpr std::uintmax_t max_file_bytes = 1024ull * 1024 * 1024;

struct SaveOptions {
    // flush the data to the disk before it replaces the file (off for the
    // recovery copies, which are rewritten every few seconds).
    bool sync = true;
    // permission bits of a file that didn't exist yet.
    unsigned new_file_mode = 0644;
};

// writes `data` to a temporary file next to `target`, flushes it to disk and
// renames it over `target`, so the file is always either fully old or fully
// new. Keeps the permission bits, the group and symlinks; refuses read-only
// files. A file that can't be replaced that way (a folder without write
// permission, a file with hard links or another owner) is written in place.
IoResult save_atomic(const std::filesystem::path& target, std::string_view data, const SaveOptions& options = {});

// the whole file, or nothing (and `error` set) when it can't be read: a
// missing or unreadable file, something that isn't a regular file (a folder,
// a pipe, a device), or one larger than max_file_bytes.
std::optional<std::string> read_text_file(const std::filesystem::path& path, std::string& error);

// an entry of a folder, with what it is (a link counts as what it points to).
struct DirEntry {
    std::filesystem::path path;
    bool is_directory = false;
    bool is_regular_file = false;
};

// the entries of `dir`, sorted case-insensitively. Never throws: on failure
// `error` is set and what could be read is returned.
std::vector<DirEntry> list_directory_entries(const std::filesystem::path& dir, std::string& error);
// the same, as paths only.
std::vector<std::filesystem::path> list_directory(const std::filesystem::path& dir, std::string& error);

// what a file looked like on disk at one moment: enough to notice later that
// something else changed it.
struct DiskStamp {
    bool exists = false;
    std::int64_t mtime_seconds = 0;
    std::int64_t mtime_nanoseconds = 0;
    std::uint64_t size = 0;
    std::uint64_t inode = 0;

    bool operator==(const DiskStamp& other) const {
        return exists == other.exists && mtime_seconds == other.mtime_seconds &&
               mtime_nanoseconds == other.mtime_nanoseconds && size == other.size && inode == other.inode;
    }
    bool operator!=(const DiskStamp& other) const { return !(*this == other); }
};

// the stamp of `path` now (exists == false when there is no such file).
DiskStamp disk_stamp(const std::filesystem::path& path);
