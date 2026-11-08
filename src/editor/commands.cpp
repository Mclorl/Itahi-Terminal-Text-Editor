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

    std::string trim(const std::string& s) {
        const std::size_t first = s.find_first_not_of(' ');
        if (first == std::string::npos) {
            return "";
        }
        return s.substr(first, s.find_last_not_of(' ') - first + 1);
    }

    void redraw() {
        if (request_redraw) {
            request_redraw();
        }
    }

    // writes `document` to its file and marks it saved.
    CommandResult save_document(Document& document, bool force) {
        if (!force && disk_changed_since_read(document.path())) {
            return {false, "File changed on disk since it was opened (use >w! to overwrite)"};
        }
        const IoResult result = save_atomic(document.path(), document.text());
        if (!result.ok) {
            return {false, "Could not save " + result.error};
        }
        document.mark_saved();
        remember_disk_stamp(document.path());
        if (request_git_refresh) {
            request_git_refresh();
        }
        redraw();
        return {true, "Saved " + document.path()};
    }

    // the path the way the explorer writes it ("./src/a.cpp" when the editor
    // opened "."), so the open file and its folders match the explorer's rows.
    fs::path explorer_form(const fs::path& file) {
        const fs::path relative = file.lexically_relative(workspace.root);
        if (relative.empty() || *relative.begin() == "..") {
            return file;  // outside the opened folder
        }
        return fs::path(workspace.root) / relative;
    }

    // opens every folder above `file` in the explorer and re-reads them, so a
    // newly created file shows up.
    void reveal_in_explorer(const fs::path& file) {
        const fs::path relative = file.lexically_relative(workspace.root);
        if (!relative.empty() && *relative.begin() != "..") {
            fs::path folder = workspace.root;
            for (const fs::path& part : relative.parent_path()) {
                folder /= part;
                workspace.opened_folders.insert(folder.string());
            }
        }
        if (request_explorer_refresh) {
            request_explorer_refresh();
        }
        if (request_git_refresh) {
            request_git_refresh();
        }
    }

    // ">e name": open a file. A file that doesn't exist yet is created right
    // away, with any missing folders, like vscode's New File.
    CommandResult open_or_create(const std::string& typed, bool) {
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
        std::string error;
        if (!workspace.open(path, error)) {
            return {false, error};
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
    CommandResult save_as(Document& document, const std::string& typed, bool force) {
        const std::string name = explorer_form(resolve_user_path(typed)).string();
        if (name != document.path() && workspace.find(name)) {
            return {false, name + " is open in another tab (>b " + fs::path(name).filename().string() + " shows it)"};
        }
        std::error_code ec;
        const bool created = !fs::exists(name, ec);
        if (!force && !created) {
            return {false, name + " already exists (use >w! " + typed + " to overwrite)"};
        }
        std::string folder_error;
        if (!create_parent_folders(name, folder_error)) {
            return {false, "Could not create " + folder_error};
        }
        const IoResult result = save_atomic(name, document.text());
        if (!result.ok) {
            return {false, "Could not save " + result.error};
        }
        // the edits now belong to the new file.
        document.set_path(name);
        document.mark_saved();
        remember_disk_stamp(name);
        if (created) {
            reveal_in_explorer(name);
        } else if (request_git_refresh) {
            request_git_refresh();
        }
        redraw();
        return {true, "Saved " + name};
    }

    // ">123": move the cursor to the start of line 123.
    CommandResult go_to_line(const std::string& number) {
        Document* document = workspace.active();
        if (!document) {
            return {false, "No file is open"};
        }
        std::size_t target = 0;
        try {
            target = static_cast<std::size_t>(std::max(1L, std::stol(number)));
        } catch (const std::exception&) {
            return {false, "Not a line number: " + number};
        }
        document->set_cursor(document->line_start(std::min(target, document->line_count()) - 1));
        return {true, ""};
    }

    // >s/old/new/[g] on the cursor's line, >%s/old/new/[g] on the whole file.
    CommandResult substitute(const std::string& command) {
        Document* document = workspace.active();
        if (!document) {
            return {false, "No file is open"};
        }
        const std::string& content = document->text();
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

        const std::size_t cursor = document->cursor();
        std::size_t begin = 0;
        std::size_t end = content.size();
        if (!whole_file) {
            const std::size_t line = document->line_of(cursor);
            begin = document->line_start(line);
            end = line + 1 < document->line_count() ? document->line_start(line + 1) - 1 : content.size();
        }

        std::string replaced;
        std::size_t count = 0;
        const std::string region = content.substr(begin, end - begin);
        try {
            const std::regex re(old_text, std::regex::ECMAScript);
            count = static_cast<std::size_t>(std::distance(std::sregex_iterator(region.begin(), region.end(), re), std::sregex_iterator()));
            if (count > 0) {
                // per line, so that without "g" only the first match of each line changes (like vim).
                std::size_t line_start = 0;
                while (line_start <= region.size()) {
                    std::size_t line_end = region.find('\n', line_start);
                    const bool last = line_end == std::string::npos;
                    if (last) line_end = region.size();
                    const std::string line = region.substr(line_start, line_end - line_start);
                    replaced += std::regex_replace(line, re, new_text,
                        global ? std::regex_constants::format_default : std::regex_constants::format_first_only);
                    if (last) break;
                    replaced += '\n';
                    line_start = line_end + 1;
                }
            }
        } catch (const std::regex_error& error) {
            return {false, std::string("Invalid pattern: ") + error.what()};
        }
        if (count == 0) {
            return {false, "Pattern not found: " + old_text};
        }

        // one undo step; the cursor stays where it was.
        document->replace(begin, end, replaced, false);
        document->set_cursor(cursor);
        return {true, "Substituted " + std::to_string(count) + (count == 1 ? " match" : " matches")};
    }

    // ">b name": show the open file called `name` (or the only one whose name starts with it).
    CommandResult show_open_file(const std::string& name, bool) {
        std::vector<Document*> matches;
        for (const auto& document : workspace.documents()) {
            if (document->name() == name || document->path() == name) {
                matches = {document.get()};
                break;
            }
            if (document->name().rfind(name, 0) == 0) {
                matches.push_back(document.get());
            }
        }
        if (matches.empty()) {
            return {false, "No open file called " + name + " (>ls lists them)"};
        }
        if (matches.size() > 1) {
            std::string names;
            for (const Document* document : matches) {
                names += "  " + document->name();
            }
            return {false, "Which one?" + names};
        }
        workspace.activate(matches[0]);
        if (show_editor) {
            show_editor();
        }
        return {true, matches[0]->path()};
    }

    // ">ls": the open files, the shown one in [brackets], unsaved ones with '*'.
    CommandResult list_open_files(const std::string&, bool) {
        if (workspace.documents().empty()) {
            return {true, "No file is open"};
        }
        std::string list;
        int number = 1;
        for (const auto& document : workspace.documents()) {
            std::string entry = std::to_string(number++) + " " + document->name() + (document->unsaved() ? "*" : "");
            if (document.get() == workspace.active()) {
                entry = "[" + entry + "]";
            }
            list += (list.empty() ? "" : "  ") + entry;
        }
        return {true, list};
    }

    CommandResult write_command(const std::string& argument, bool force) {
        Document* document = workspace.active();
        if (!argument.empty() && document) {
            return save_as(*document, argument, force);
        }
        return save_active_file(force);
    }

    CommandResult write_all_command(const std::string&, bool force) {
        for (const auto& document : workspace.documents()) {
            if (!document->unsaved()) {
                continue;
            }
            const CommandResult saved = save_document(*document, force);
            if (!saved.ok) {
                return saved;
            }
        }
        return {true, "Saved every file"};
    }

    CommandResult quit_command(const std::string&, bool force) {
        if (!force) {
            if (const Document* unsaved = workspace.first_unsaved()) {
                return {false, "Unsaved changes in " + unsaved->path() + " (>wa saves all, add ! to quit anyway)"};
            }
        }
        if (request_quit) {
            request_quit();
        }
        return {true, ""};
    }

    CommandResult write_quit_command(const std::string&, bool force) {
        // the write failed and the changes are still only in memory: stop
        // here so the quit can't lose them.
        const CommandResult saved = save_active_file(force);
        if (!saved.ok) {
            return saved;
        }
        const CommandResult quit = quit_command("", force);
        return quit.ok ? saved : quit;
    }

    CommandResult refresh_command(const std::string&, bool) {
        if (request_explorer_refresh) {
            request_explorer_refresh();
        }
        if (request_git_refresh) {
            request_git_refresh();
        }
        return {true, "Explorer and Git status read again"};
    }

    enum class Argument { None, Optional, Required };

    struct EditorCommand {
        Argument argument;  // a file name (or another word) after the command
        bool accepts_force; // whether a '!' may follow the name
        CommandResult (*run)(const std::string& argument, bool force);
    };

    // every command the command line understands. A name only matches as a
    // whole: "wq" is a command of its own, "qw" is not one.
    const std::map<std::string, EditorCommand> editor_commands = {
        {"w",       {Argument::Optional, true,  write_command}},      // save; "w!" even if changed on disk; "w name" saves as
        {"wa",      {Argument::None,     true,  write_all_command}},  // save every unsaved file
        {"q",       {Argument::None,     true,  quit_command}},       // quit, "q!" even with unsaved files
        {"wq",      {Argument::None,     true,  write_quit_command}}, // save, then quit
        {"e",       {Argument::Required, false, open_or_create}},     // open (or create) a file
        {"u",       {Argument::None,     false, [](const std::string&, bool) { return undo_edit(); }}},
        {"undo",    {Argument::None,     false, [](const std::string&, bool) { return undo_edit(); }}},
        {"red",     {Argument::None,     false, [](const std::string&, bool) { return redo_edit(); }}},
        {"redo",    {Argument::None,     false, [](const std::string&, bool) { return redo_edit(); }}},
        {"bn",      {Argument::None,     false, [](const std::string&, bool) { return switch_file(+1); }}},  // next tab
        {"bp",      {Argument::None,     false, [](const std::string&, bool) { return switch_file(-1); }}},  // previous tab
        {"b",       {Argument::Required, false, show_open_file}},     // the open file with this name
        {"bd",      {Argument::None,     true,  [](const std::string&, bool force) { return close_active_file(force); }}},
        {"ls",      {Argument::None,     false, list_open_files}},
        {"refresh", {Argument::None,     false, refresh_command}},    // read the folders and Git status again
    };

}  // namespace

CommandResult save_active_file(bool force) {
    Document* document = workspace.active();
    if (!document) {
        return {false, "No file is open to save"};
    }
    return save_document(*document, force);
}

CommandResult close_active_file(bool force) {
    Document* document = workspace.active();
    if (!document) {
        return {false, "No file is open"};
    }
    if (!force && document->unsaved()) {
        return {false, "Unsaved changes in " + document->path() + " (>w saves, >bd! closes without saving)"};
    }
    const std::string path = document->path();
    workspace.close(document);
    redraw();
    return {true, ""};
}

CommandResult switch_file(int step) {
    if (!workspace.cycle(step)) {
        return {false, workspace.documents().empty() ? "No file is open" : "Only one file is open"};
    }
    if (show_editor) {
        show_editor();
    }
    return {true, ""};  // the tab bar shows which file it is
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

    // a command is a name made of letters, optionally followed by '!', then
    // optionally an argument (a file name).
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
        return {false, "Argument required: >" + name + (name == "b" ? " name of an open file" : " file name")};
    }
    return action.run(argument, force);
}
