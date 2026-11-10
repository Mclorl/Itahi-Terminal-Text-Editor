#pragma once

#include <filesystem>
#include <string>

#include "core/file_io.hpp"

// deleting and renaming files and folders.

// moves `path` (a file, or a folder with everything in it) to the desktop's
// trash, where it can be restored from: the freedesktop.org Trash
// specification, as file managers use it ($XDG_DATA_HOME/Trash, or
// .Trash-<uid> at the top of another disk). On success `trashed_as` is where
// it went.
IoResult move_to_trash(const std::filesystem::path& path, std::filesystem::path* trashed_as = nullptr);

// deletes a file, or a folder that is empty, for good.
IoResult delete_for_good(const std::filesystem::path& path);

// renames (moves) `from` to `to`. Never replaces something that is already
// there: that is an error.
IoResult rename_path(const std::filesystem::path& from, const std::filesystem::path& to);
