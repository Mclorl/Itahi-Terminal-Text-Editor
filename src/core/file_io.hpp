#pragma once

// files on disk: reading, atomic saving, listing folders, noticing outside
// changes. Standard library and POSIX only.

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

// writes `data` to a temporary file next to `target`, flushes it to disk and
// renames it over `target`, so the file is always either fully old or fully
// new. Keeps the permission bits and symlinks; refuses read-only files.
IoResult save_atomic(const std::filesystem::path& target, std::string_view data);

// the whole file, or nothing (and `error` set) when it can't be read.
std::optional<std::string> read_text_file(const std::filesystem::path& path, std::string& error);

// the entries of `dir`, sorted case-insensitively. Never throws: on failure
// `error` is set and what could be read is returned.
std::vector<std::filesystem::path> list_directory(const std::filesystem::path& dir, std::string& error);

// remember the modification time and size of `path` (after reading or saving),
// to notice later if something else changed the file.
void remember_disk_stamp(const std::string& path);
bool disk_changed_since_read(const std::string& path);
