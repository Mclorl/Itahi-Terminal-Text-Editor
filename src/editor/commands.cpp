#include "editor/commands.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <regex>
#include <system_error>
#include <utility>
#include <vector>

#include "core/file_io.hpp"
#include "editor/paths.hpp"
#include "editor/search.hpp"
#include "editor/state.hpp"
#include "editor/undo_history.hpp"
#include "editor/workspace.hpp"

namespace fs = std::filesystem;

namespace {

    enum class Argument { None, Optional, Required };

    struct EditorCommand {
        bool save;          // save the open file (or every unsaved file for "wa")
        bool save_all;
        bool quit;
        bool open;          // open the file named by the argument
        bool accepts_force; // whether a '!' may follow the name
        Argument argument;  // a file name after the command
    };

    // every command the command line understands. A name only matches as a
    // whole: "wq" is a command of its own, "qw" is not one.
    const std::map<std::string, EditorCommand> editor_commands = {
        {"w",  {true,  false, false, false, true,  Argument::Optional}}, // save; "w!" even if changed on disk; "w name" saves as
        {"wa", {true,  true,  false, false, true,  Argument::None}},     // save every unsaved file
        {"q",  {false, false, true,  false, true,  Argument::None}},     // quit, "q!" even with unsaved files
        {"wq", {true,  false, true,  false, true,  Argument::None}},     // save, then quit
        {"e",  {false, false, false, true,  false, Argument::Required}}, // open (or create) a file
    };

    std::string trim(const std::string& s) {
        const std::size_t first = s.find_first_not_of(' ');
        if (first == std::string::npos) {
            return "";
        }
        return s.substr(first, s.find_last_not_of(' ') - first + 1);
    }

    // saves `text` as the file at `path` and marks it saved.
    CommandResult save_file(const std::string& path, const std::string& text, bool force) {
        if (!force && disk_changed_since_read(path)) {
            return {false, "File changed on disk since it was opened (use >w! to overwrite)"};
        }
        const IoResult result = save_atomic(path, text);
        if (!result.ok) {
            return {false, "Could not save " + result.error};
        }
        content_read_first[path] = text;
        content_user_updated[path].clear();
        file_unsaved_status[path] = false;
        remember_disk_stamp(path);
        if (path == long_active_file_name) {
            active_file = active_file_saved;
        }
        if (request_redraw) {
            request_redraw();
        }
        return {true, "Saved " + path};
    }

    // the path the way the explorer writes it ("./src/a.cpp" when the editor
    // opened "."), so the open file and its folders match the explorer's rows.
    fs::path explorer_form(const fs::path& file) {
        const fs::path relative = file.lexically_relative(workspace_root);
        if (relative.empty() || *relative.begin() == "..") {
            return file;  // outside the opened folder
        }
        return fs::path(workspace_root) / relative;
    }

    // opens every folder above `file` in the explorer and re-reads them, so a
    // newly created file shows up.
    void reveal_in_explorer(const fs::path& file) {
        const fs::path relative = file.lexically_relative(workspace_root);
        if (!relative.empty() && *relative.begin() != "..") {
            fs::path folder = workspace_root;
            for (const fs::path& part : relative.parent_path()) {
                folder /= part;
                opened_folders.insert(folder.string());
            }
        }
        if (request_explorer_refresh) {
            request_explorer_refresh();
        }
    }

    // ">e name": open a file. A file that doesn't exist yet is created right
    // away, with any missing folders, like vscode's New File.
    CommandResult open_or_create(const std::string& typed) {
        const fs::path path = explorer_form(resolve_user_path(typed));
        std::error_code ec;
        const bool created = !fs::exists(path, ec);
        if (created) {
            if (typed.back() == '/') {
                return {false, "Give the new file a name: >e " + typed + "name"};
            }
            std::string error;
            if (!create_parent_folders(path, error)) {
                return {false, "Could not create " + error};
            }
            const IoResult result = save_atomic(path, "");
            if (!result.ok) {
                return {false, "Could not create " + result.error};
            }
        } else if (fs::is_directory(path, ec)) {
            return {false, path.string() + " is a folder"};
        }
        if (!open_file(path, content, active_file, long_active_file_name)) {
            return {false, command_error};
        }
        if (created) {
            reveal_in_explorer(path);
        }
        if (show_editor) {
            show_editor();
        }
        return {true, (created ? "Created " : "Opened ") + path.string()};
    }

    // ">w name": the buffer becomes the file `name` (the old file keeps what
    // is on disk).
    CommandResult save_as(const std::string& typed, bool force) {
        const std::string name = explorer_form(resolve_user_path(typed)).string();
        std::error_code ec;
        const bool created = !fs::exists(name, ec);
        if (!force && !created) {
            return {false, name + " already exists (use >w! " + typed + " to overwrite)"};
        }
        std::string folder_error;
        if (!create_parent_folders(name, folder_error)) {
            return {false, "Could not create " + folder_error};
        }
        const std::string old_path = long_active_file_name;
        const std::string text = content;

        const CommandResult saved = save_file(name, text, true);
        if (!saved.ok) {
            return saved;
        }
        // the edits now belong to the new file.
        content_user_updated[old_path].clear();
        file_unsaved_status[old_path] = false;
        content_already_read[name] = true;

        const std::string file_name = fs::path(name).filename().string();
        active_file_not_saved = file_name + "*";
        active_file_saved = file_name;
        active_file = active_file_saved;
        file_cursor[name] = cursor_index;
        long_active_file_name = name;
        if (created) {
            reveal_in_explorer(name);
        }
        return saved;
    }

    // ">123": move the cursor to the start of line 123.
    CommandResult go_to_line(const std::string& number) {
        if (long_active_file_name.empty()) {
            return {false, "No file is open"};
        }
        int target = 0;
        try {
            target = std::stoi(number);
        } catch (const std::exception&) {
            return {false, "Not a line number: " + number};
        }
        std::size_t offset = 0;
        for (int line = 1; line < target; line++) {
            const std::size_t newline = content.find('\n', offset);
            if (newline == std::string::npos) {
                break;
            }
            offset = newline + 1;
        }
        cursor_index = static_cast<int>(offset);
        return {true, ""};
    }

}  // namespace

namespace {

    // >s/old/new/[g] on the cursor's line, >%s/old/new/[g] on the whole file.
    CommandResult substitute(const std::string& command) {
        if (long_active_file_name.empty()) {
            return {false, "No file is open"};
        }
        const bool whole_file = command[0] == '%';
        const std::string spec = command.substr(whole_file ? 2 : 1);  // after "s" / "%s"
        if (spec.empty()) {
            return {false, "Usage: >s/old/new/ or >%s/old/new/g"};
        }
        const char delimiter = spec[0];
        std::vector<std::string> parts{""};
        for (std::size_t i = 1; i < spec.size(); ++i) {
            if (spec[i] == '\\' && i + 1 < spec.size() && spec[i + 1] == delimiter) {
                parts.back() += delimiter;
                ++i;
            } else if (spec[i] == delimiter) {
                parts.emplace_back();
            } else {
                parts.back() += spec[i];
            }
        }
        if (parts.size() < 2 || parts[0].empty()) {
            return {false, "Usage: >s/old/new/ or >%s/old/new/g"};
        }
        const std::string& old_text = parts[0];
        const std::string& new_text = parts[1];
        const bool global = parts.size() > 2 && parts[2].find('g') != std::string::npos;

        std::size_t begin = 0;
        std::size_t end = content.size();
        if (!whole_file) {
            const std::size_t at = static_cast<std::size_t>(std::clamp(cursor_index, 0, static_cast<int>(content.size())));
            begin = at == 0 ? 0 : content.rfind('\n', at - 1);
            begin = begin == std::string::npos || at == 0 ? 0 : begin + 1;
            end = content.find('\n', at);
            if (end == std::string::npos) {
                end = content.size();
            }
        }

        std::string replaced;
        std::size_t count = 0;
        const std::string region = content.substr(begin, end - begin);
        try {
            const std::regex re(old_text, std::regex::ECMAScript);
            count = static_cast<std::size_t>(std::distance(std::sregex_iterator(region.begin(), region.end(), re), std::sregex_iterator()));
            if (count > 0) {
                std::string kept_lines;
                // per line, so that without "g" only the first match of each line changes (like vim).
                std::size_t line_start = 0;
                while (line_start <= region.size()) {
                    std::size_t line_end = region.find('\n', line_start);
                    const bool last = line_end == std::string::npos;
                    if (last) line_end = region.size();
                    const std::string line = region.substr(line_start, line_end - line_start);
                    kept_lines += std::regex_replace(line, re, new_text,
                        global ? std::regex_constants::format_default : std::regex_constants::format_first_only);
                    if (last) break;
                    kept_lines += '\n';
                    line_start = line_end + 1;
                }
                replaced = kept_lines;
            }
        } catch (const std::regex_error& error) {
            return {false, std::string("Invalid pattern: ") + error.what()};
        }
        if (count == 0) {
            return {false, "Pattern not found: " + old_text};
        }

        record_command_edit();
        content.replace(begin, end - begin, replaced);
        cursor_index = std::min(cursor_index, static_cast<int>(content.size()));
        refresh_unsaved_status(content);
        return {true, "Substituted " + std::to_string(count) + (count == 1 ? " match" : " matches")};
    }

}  // namespace

CommandResult save_active_file(bool force) {
    if (long_active_file_name.empty()) {
        return {false, "No file is open to save"};
    }
    return save_file(long_active_file_name, content, force);
}

CommandResult run_command(std::string command) {
    command = trim(command);
    if (command.empty()) {
        return {true, ""};
    }
    if (std::all_of(command.begin(), command.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return go_to_line(command);
    }
    // like vim: /pattern searches down, ?pattern up, s and %s substitute.
    if (command[0] == '/' || command[0] == '?') {
        const CommandResult found = search(command.substr(1), command[0] == '?');
        if (found.ok && show_editor) {
            show_editor();
        }
        return found;
    }
    if (command.rfind("%s", 0) == 0 || (command.size() > 1 && command[0] == 's' && !std::isalpha(static_cast<unsigned char>(command[1])))) {
        return substitute(command);
    }
    if (command == "u" || command == "undo") {
        return undo_edit();
    }
    if (command == "red" || command == "redo") {
        return redo_edit();
    }

    // a command is a name made of letters, optionally followed by '!', then
    // optionally a file name.
    std::size_t name_end = 0;
    while (name_end < command.size() && std::isalpha(static_cast<unsigned char>(command[name_end]))) {
        name_end++;
    }
    const std::string name = command.substr(0, name_end);
    std::string rest = command.substr(name_end);

    const auto found = editor_commands.find(name);
    if (name.empty() || found == editor_commands.end()) {
        return {false, "Not an editor command: " + command};
    }
    const EditorCommand& action = found->second;

    const bool force = !rest.empty() && rest[0] == '!';
    if (force) {
        rest.erase(0, 1);
    }
    if (!rest.empty() && rest[0] != ' ') {
        return {false, "Trailing characters: " + rest};
    }
    const std::string argument = trim(rest);

    if (force && !action.accepts_force) {
        return {false, "No ! allowed: " + command};
    }
    if (!argument.empty() && action.argument == Argument::None) {
        return {false, "Trailing characters: " + argument};
    }
    if (argument.empty() && action.argument == Argument::Required) {
        return {false, "Argument required: >" + name + " file name"};
    }

    if (action.open) {
        return open_or_create(argument);
    }

    std::string message;
    if (action.save_all) {
        for (const auto& [file, unsaved] : file_unsaved_status) {
            if (!unsaved) {
                continue;
            }
            const std::string text = file == long_active_file_name ? content : content_user_updated[file];
            const CommandResult saved = save_file(file, text, force);
            if (!saved.ok) {
                return saved;
            }
        }
        message = "Saved every file";
    } else if (action.save) {
        const CommandResult saved = argument.empty() ? save_active_file(force) : save_as(argument, force);

        // the write failed and the changes are still only in memory: stop
        // here so the quit of "wq" can't lose them.
        if (!saved.ok) {
            return saved;
        }
        message = saved.message;
    }

    if (action.quit) {
        if (!force) {
            for (const auto& [file, unsaved] : file_unsaved_status) {
                if (unsaved) {
                    return {false, "Unsaved changes in " + (file.empty() ? std::string("the open buffer") : file) + " (>wa saves all, add ! to quit anyway)"};
                }
            }
        }
        if (request_quit) {
            request_quit();
        }
    }

    return {true, message};
}
