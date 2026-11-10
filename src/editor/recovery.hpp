#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class Document;

// recovery copies: the unsaved text of every changed file, written to the
// state folder a moment after typing stops. When the editor is killed, the
// terminal is closed or the computer loses power, the next start offers them
// back (">recover"). A copy is removed when its file is saved, or when its
// changes are dropped on purpose (">bd!", ">q!").
//
// The copies are private: the folder is made for the user alone (0700), the
// files too (0600).

// $XDG_STATE_HOME/itahi/recovery, or ~/.local/state/itahi/recovery.
std::filesystem::path recovery_folder();
// keeps the copies somewhere else (tests).
void set_recovery_folder(std::filesystem::path folder);

struct RecoveryCopy {
    std::string path;            // the file it is a copy of (absolute)
    std::filesystem::path file;  // where the copy is kept
};

// every copy there is, by the path of its file.
std::vector<RecoveryCopy> recovery_copies();

// the text kept in a copy; nothing (and `error` set) when it can't be read.
std::optional<std::string> read_recovery_copy(const RecoveryCopy& copy, std::string& error);

// writes the copy of `document` now. False, with `error` set, when it can't
// be written.
bool keep_recovery_copy(Document& document, std::string& error);

// removes the copy of `document`, or of the file at `path`.
void drop_recovery_copy(Document& document);
void drop_recovery_copy_of(const std::string& path);

// called every second or so: writes the copy of every document whose unsaved
// text changed and has been left alone for `idle`, and removes the copies of
// documents that are saved again. Returns an error to show, "" when all went
// well. Does nothing while settings.keep_recovery_copies is off.
std::string update_recovery_copies(std::chrono::milliseconds idle = std::chrono::milliseconds(1500));

// writes the copy of every unsaved document at once (the editor is being
// closed from outside). Returns how many were written.
int keep_all_recovery_copies();
