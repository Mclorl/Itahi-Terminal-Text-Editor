#include "editor/state.hpp"

#include <algorithm>

#include <string>
#include <unordered_set>
#include <vector>

#include "core/encoding.hpp"

std::string active_file;

std::string active_file_not_saved;
std::string active_file_saved;

std::string long_active_file_name;

std::unordered_set<std::string> opened_folders;
std::string workspace_root = ".";

// command mode line
std::string command_line;
int command_cursor_index = 0;
std::string command_output;
std::string command_error;
std::vector<std::string> command_completions;

// mouse sensitivity
std::string mouse_sensitivity = "3";

// spaces inserted by Tab in edit mode
std::string tab_size = "4";

// -- view --
bool show_line_numbers = true;
bool underline_active_row_on_numbers = false;
bool underline_active_row = false;
bool syntax_highlighting = true;
bool word_wrap = false;

// global content
std::string content;
int cursor_index = 0;

std::unordered_map<std::string, bool> content_already_read;
std::unordered_map<std::string, bool> file_truncate_name_bool_status;
std::unordered_map<std::string, bool> file_unsaved_status;
std::unordered_map<std::string, std::string> content_read_first;
std::unordered_map<std::string, std::string> content_user_updated;
std::unordered_map<std::string, int> file_cursor;

std::unordered_set<std::string> files_opened_anyway;

std::function<void()> request_redraw;
std::function<void()> request_quit;
std::function<void()> show_editor;
std::function<void()> request_explorer_refresh;

Mode mode = Mode::Normal;

void refresh_unsaved_status(const std::string &text) {
    if (content_read_first[long_active_file_name] != text) {
        content_user_updated[long_active_file_name] = text;
        active_file = active_file_not_saved;
        file_unsaved_status[long_active_file_name] = true;
    } else {
        content_user_updated[long_active_file_name].clear();
        active_file = active_file_saved;
        file_unsaved_status[long_active_file_name] = false;
    }
}

bool check_content_directory_for_unsaved_file(std::string path) {
    // only files opened in the editor can be unsaved, and their status is
    // already in memory: look for one under this folder. (This runs on every
    // render of every folder row; walking the folder on disk instead was very
    // slow for big folders such as build/.)
    if (path.empty() || path.back() != '/') {
        path += '/';
    }

    for (const auto& [file, unsaved] : file_unsaved_status) {
        if (unsaved && file.compare(0, path.size(), path) == 0) {
            return true;
        }
    }

    return false;
}

bool is_file_hidden_from_editor(const std::string& path) {
    if (path.empty() || files_opened_anyway.count(path) != 0) {
        return false;
    }

    // checked once per file: this runs on every render.
    static std::unordered_map<std::string, bool> checked;
    const auto found = checked.find(path);
    if (found != checked.end()) {
        return found->second;
    }
    return checked[path] = is_binary_or_unsupported_text(content_read_first[path]);
}
