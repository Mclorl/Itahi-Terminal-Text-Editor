#pragma once

#include <filesystem>
#include <string>
#include <vector>

// paths typed in the command line (>e name, >w name).
//   /dir/file   inside the folder Itahi opened (`workspace.root`), like a
//               project-relative path in vscode
//   ~/dir/file  inside the home folder
//   name        a plain name (no '/'): inside the folder Itahi opened. The
//               active folder is offered as a ghost hint after ">e" (Tab)
//   dir/file    relative, the way the explorer shows paths (eg. ./src/a.cpp)
std::filesystem::path resolve_user_path(const std::string& typed);

// the path the way the explorer writes it ("./src/a.cpp" when the editor
// opened "."), however it was spelled ("src/../src/a.cpp", an absolute path,
// "~/project/src/a.cpp"), so a file is one document whichever way it is
// opened. A path outside the opened folder is returned as it is.
std::filesystem::path explorer_form(const std::filesystem::path& file);

// the absolute path without ".", ".." or a '/' at its end: the form paths
// are compared in.
std::string absolute_normal(const std::filesystem::path& path);

// creates every missing folder above `file`. Returns false (and sets `error`)
// when one can't be created.
bool create_parent_folders(const std::filesystem::path& file, std::string& error);

// Tab completion of a typed path, like a shell's `cd`.
struct PathCompletion {
    std::string text;                  // the typed path, completed as far as it is certain
    std::vector<std::string> options;  // every possible name when there is more than one
};

// the folder a plain name typed in a command goes to: the active folder when
// it still exists, else the nearest folder above it that does.
std::filesystem::path current_new_file_folder();

// that folder the way it is typed after ">e ": "src/" or "docs/drafts/", with
// no leading "./"; "" when it is the folder Itahi was started in.
std::string active_folder_for_command();

// completes the last part of `typed` with the folders and files that start
// with it. Folders end with '/'. Hidden entries only when the typed part
// starts with '.'.
PathCompletion complete_path(const std::string& typed);
