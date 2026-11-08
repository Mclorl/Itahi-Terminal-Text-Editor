#pragma once

#include <filesystem>
#include <string>
#include <vector>

// paths typed in the command line (>e name, >w name).
//   /dir/file   inside the folder Itahi opened (`workspace_root`), like a
//               project-relative path in vscode
//   ~/dir/file  inside the home folder
//   dir/file    relative, the way the explorer shows paths (eg. ./src/a.cpp)
std::filesystem::path resolve_user_path(const std::string& typed);

// creates every missing folder above `file`. Returns false (and sets `error`)
// when one can't be created.
bool create_parent_folders(const std::filesystem::path& file, std::string& error);

// Tab completion of a typed path, like a shell's `cd fol<Tab>`.
struct PathCompletion {
    std::string text;                  // the typed path, completed as far as it is certain
    std::vector<std::string> options;  // every possible name when there is more than one
};

// completes the last part of `typed` with the folders and files that start
// with it. Folders end with '/'. Hidden entries only when the typed part
// starts with '.'.
PathCompletion complete_path(const std::string& typed);
