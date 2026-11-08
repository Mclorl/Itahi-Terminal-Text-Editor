#include "editor/undo_history.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/state.hpp"

namespace {

    // a snapshot of the buffer is taken before an edit, unless the previous
    // edit was less than a moment ago: typing a word is one undo step.
    struct Snapshot {
        std::string text;
        int cursor;
    };

    struct History {
        std::vector<Snapshot> undo;
        std::vector<Snapshot> redo;
        std::chrono::steady_clock::time_point last_edit;
    };

    constexpr std::size_t max_undo_steps = 200;
    constexpr auto typing_pause = std::chrono::milliseconds(800);

    std::unordered_map<std::string, History> histories;

}  // namespace

void record_edit() {
    History& history = histories[long_active_file_name];
    const auto now = std::chrono::steady_clock::now();
    if (history.undo.empty() || now - history.last_edit > typing_pause) {
        history.undo.push_back({content, cursor_index});
        if (history.undo.size() > max_undo_steps) {
            history.undo.erase(history.undo.begin());
        }
    }
    history.last_edit = now;
    history.redo.clear();
}

// an edit made by a command: always its own undo step.
void record_command_edit() {
    histories[long_active_file_name].last_edit = {};
    record_edit();
    histories[long_active_file_name].last_edit = {};
}

CommandResult undo_edit() {
    if (long_active_file_name.empty()) {
        return {false, "No file is open"};
    }
    History& history = histories[long_active_file_name];
    if (history.undo.empty()) {
        return {false, "Nothing to undo"};
    }
    history.redo.push_back({content, cursor_index});
    content = std::move(history.undo.back().text);
    cursor_index = history.undo.back().cursor;
    history.undo.pop_back();
    history.last_edit = {};  // the next edit starts a new step
    refresh_unsaved_status(content);
    return {true, "Undo"};
}

CommandResult redo_edit() {
    if (long_active_file_name.empty()) {
        return {false, "No file is open"};
    }
    History& history = histories[long_active_file_name];
    if (history.redo.empty()) {
        return {false, "Nothing to redo"};
    }
    history.undo.push_back({content, cursor_index});
    content = std::move(history.redo.back().text);
    cursor_index = history.redo.back().cursor;
    history.redo.pop_back();
    history.last_edit = {};
    refresh_unsaved_status(content);
    return {true, "Redo"};
}
