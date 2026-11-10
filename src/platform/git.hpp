#pragma once

#include <map>
#include <string>
#include <string_view>

// what Git says about the opened folder: the branch, and which files are
// changed, new or ignored. Asked with `git status`; nothing when the folder
// isn't in a Git repository or git isn't installed.

struct GitStatus {
    bool in_repository = false;
    std::string top_level;  // the repository's root folder (absolute)
    std::string branch;  // "main"; "HEAD (detached)" when on no branch
    int ahead = 0;       // commits not pushed yet
    int behind = 0;      // commits not pulled yet
    // the state of every changed path, by absolute path (lexically normal):
    //   'M' modified, 'A' added, 'D' deleted, 'R' renamed, 'U' unmerged (conflict),
    //   '?' new (untracked), '!' ignored. A path ending in '/' is a whole folder.
    std::map<std::string, char> paths;

    // the state of `path` (the way the explorer lists it), ' ' when unchanged.
    char state_of(const std::string& path) const;
    // true when a changed (not ignored) path is inside `folder`.
    bool changes_under(const std::string& folder) const;
    // true when `path` or a folder above it is ignored.
    bool ignored(const std::string& path) const;
    // counts for the status bar.
    int changed_count() const;
};

// runs git in `folder` (blocking: a few milliseconds in most repositories).
GitStatus read_git_status(const std::string& folder);

// reads the output of `git status --porcelain=v1 -z --branch --ignored`, whose
// paths are relative to `top_level` (the repository's root).
GitStatus parse_git_status(std::string_view porcelain, const std::string& top_level);

// the absolute, lexically normal form used as the key of GitStatus::paths.
std::string git_path_key(const std::string& path);
