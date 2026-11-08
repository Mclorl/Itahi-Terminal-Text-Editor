#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// the editor's state: the open file, every file's buffer and flags, the
// settings and the mode. No FTXUI here: the UI reads and changes this state,
// and installs the hooks below so the editor can ask it for things.

extern std::string active_file;
extern std::string active_file_not_saved;
extern std::string active_file_saved;

extern std::string long_active_file_name;

// the buffer of the open file and the cursor in it (a byte offset).
extern std::string content;
extern int cursor_index;

extern std::unordered_set<std::string> opened_folders;

// the folder Itahi opened (the explorer's top). "/name" in a command means
// a path inside it (see editor/paths.hpp).
extern std::string workspace_root;

// command mode: the text of the command line (always starts with the '>'
// prompt), its cursor, the result of the last command shown in its place, and
// the error of a rejected command, shown above the command line.
extern std::string command_line;
extern int command_cursor_index;
extern std::string command_output;
extern std::string command_error;
// the names Tab could complete to, shown under the command line until the
// next key.
extern std::vector<std::string> command_completions;

// settings, as typed in the Settings tab (see editor/settings.hpp).
extern std::string mouse_sensitivity;
extern std::string tab_size;

// -- view --
extern bool show_line_numbers;
extern bool underline_active_row_on_numbers;
extern bool underline_active_row;
extern bool syntax_highlighting;
extern bool word_wrap;

// per-file state, keyed by path.
extern std::unordered_map<std::string, bool> content_already_read;
extern std::unordered_map<std::string, bool> file_truncate_name_bool_status;
extern std::unordered_map<std::string, bool> file_unsaved_status;
extern std::unordered_map<std::string, std::string> content_read_first;
extern std::unordered_map<std::string, std::string> content_user_updated;
extern std::unordered_map<std::string, int> file_cursor;

// binary / unsupported encoding files the user chose to "Open Anyway".
extern std::unordered_set<std::string> files_opened_anyway;

// the modes that are available: Normal | Edit | Command. One value, so the
// modes can never disagree.
enum class Mode { Normal, Edit, Command };
extern Mode mode;

// hooks the UI installs (main.cpp). Each may be empty.
extern std::function<void()> request_redraw;  // draw a new frame
extern std::function<void()> request_quit;    // leave the main loop
extern std::function<void()> show_editor;     // close any menu, focus the editor
extern std::function<void()> request_explorer_refresh;  // re-read the folders shown in the explorer

// after an edit of the open file: marks it unsaved (or saved again when it
// matches the file on disk) and keeps its edited text.
void refresh_unsaved_status(const std::string& text);

// true when a file under the folder `path` has unsaved changes.
bool check_content_directory_for_unsaved_file(std::string path);

// true while the file at `path` must stay hidden behind the "Open Anyway"
// warning (binary / unsupported encoding, and not opened anyway yet).
bool is_file_hidden_from_editor(const std::string& path);
