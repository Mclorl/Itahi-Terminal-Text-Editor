#include "editor/commands.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <system_error>
#include <utility>
#include <vector>

#include "core/file_io.hpp"
#include "core/trash.hpp"
#include "editor/paths.hpp"
#include "editor/recovery.hpp"
#include "editor/search.hpp"
#include "editor/settings.hpp"
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

    // --- files ---------------------------------------------------------------

    // writes `document` to its file and marks it saved.
    CommandResult save_document(Document& document, bool force) {
        if (!force && document.changed_on_disk()) {
            if (document.disk.exists && !disk_stamp(document.path()).exists) {
                return {false, document.path() + " was deleted on disk (use >w! to write it again)"};
            }
            return {false, "File changed on disk since it was opened (use >w! to overwrite, >e! to read it again)"};
        }
        if (!document.exists_on_disk()) {
            // a new file, maybe in a folder that isn't there yet.
            std::string folder_error;
            if (!create_parent_folders(document.path(), folder_error)) {
                return {false, "Could not create " + folder_error};
            }
        }
        const bool created = !document.exists_on_disk();
        const IoResult result = save_atomic(document.path(), document.text());
        if (!result.ok) {
            return {false, "Could not save " + result.error};
        }
        document.mark_saved();
        document.disk = disk_stamp(document.path());
        document.disk_seen.reset();
        drop_recovery_copy(document);
        if (created) {
            call(hooks.refresh_explorer);
        }
        call(hooks.refresh_git);
        call(hooks.redraw);
        return {true, "Saved " + document.path()};
    }

    // true when `path` is `folder` or inside it.
    bool is_or_under(const std::string& path, const std::string& folder) {
        return path == folder || path.compare(0, folder.size() + 1, folder + "/") == 0;
    }

    // true when deleting or moving `path` would take the opened folder with it.
    bool holds_opened_folder(const fs::path& path) {
        return is_or_under(absolute_normal(workspace.root), absolute_normal(path));
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
        call(hooks.refresh_explorer);
        call(hooks.refresh_git);
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
        std::string error;
        if (!workspace.open(path, error)) {
            return {false, error};
        }
        if (created) {
            reveal_in_explorer(path);
        }
        call(hooks.show_editor);
        return {true, (created ? "Created " : "Opened ") + path.string()};
    }

    // ">e" / ">e!" without a name read the open file again; with one, open it.
    CommandResult edit_command(const std::string& argument, bool force) {
        if (!argument.empty()) {
            if (force) {
                return {false, "No ! allowed with a file name: >e " + argument};
            }
            return open_or_create(argument);
        }
        Document* document = workspace.active();
        if (!document) {
            return {false, "Argument required: >e file name"};
        }
        return reload_document(*document, force);
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
        if (!created && fs::is_directory(name, ec)) {
            return {false, name + " is a folder"};
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
        drop_recovery_copy(document);
        document.set_path(name);
        document.mark_saved();
        document.disk = disk_stamp(name);
        document.disk_seen.reset();
        if (created) {
            reveal_in_explorer(name);
        } else {
            call(hooks.refresh_git);
        }
        call(hooks.redraw);
        return {true, "Saved " + name};
    }

    CommandResult write_command(const std::string& argument, bool force) {
        Document* document = workspace.active();
        if (!argument.empty() && document) {
            return save_as(*document, argument, force);
        }
        return save_active_file(force);
    }

    CommandResult write_all_command(const std::string&, bool force) {
        int saved = 0;
        for (const auto& document : workspace.documents()) {
            if (!document->unsaved()) {
                continue;
            }
            const CommandResult result = save_document(*document, force);
            if (!result.ok) {
                return {false, document->path() + ": " + result.message};
            }
            ++saved;
        }
        return {true, saved == 0 ? "Nothing to save" : "Saved " + std::to_string(saved) + (saved == 1 ? " file" : " files")};
    }

    CommandResult quit_command(const std::string&, bool force) {
        if (!force) {
            if (const Document* unsaved = workspace.first_unsaved()) {
                return {false, "Unsaved changes in " + unsaved->path() + " (>wa saves all, add ! to quit anyway)"};
            }
        } else {
            // quitting without saving is a choice: nothing to offer back later.
            for (const auto& document : workspace.documents()) {
                drop_recovery_copy(*document);
            }
        }
        call(hooks.quit);
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
        call(hooks.refresh_explorer);
        call(hooks.refresh_git);
        const std::string changed = check_files_on_disk();
        return {true, changed.empty() ? "Explorer and Git status read again" : changed};
    }

    // --- the explorer's files and folders ------------------------------------

    // what a command without a path acts on: the row selected in the explorer
    // while it has the focus, else the open file.
    std::string command_target() {
        const std::string selected = hooks.command_target ? hooks.command_target() : std::string();
        return selected.empty() ? active_path() : selected;
    }

    CommandResult make_folder_command(const std::string& argument, bool) {
        const fs::path path = explorer_form(resolve_user_path(argument)).lexically_normal();
        const fs::path shown = explorer_form(path);
        std::error_code ec;
        if (fs::exists(shown, ec)) {
            return {false, shown.string() + " already exists"};
        }
        fs::create_directories(shown, ec);
        if (ec) {
            return {false, "Could not create " + shown.string() + ": " + ec.message()};
        }
        // shown, and open: the next new file is most likely for it.
        workspace.opened_folders.insert(shown.string());
        reveal_in_explorer(shown / "x");
        return {true, "Created the folder " + shown.string()};
    }

    CommandResult rename_command(const std::string& argument, bool) {
        const std::string from = command_target();
        if (from.empty()) {
            return {false, "Nothing to rename: open a file, or select one in the explorer"};
        }
        if (holds_opened_folder(from)) {
            return {false, "The opened folder can't be renamed from inside Itahi"};
        }
        // a plain name stays in the same folder; a name ending in '/' is the
        // folder to move it into; anything else is the new path.
        fs::path to;
        if (argument.find('/') == std::string::npos) {
            to = fs::path(from).parent_path() / argument;
        } else if (argument.back() == '/') {
            to = explorer_form(resolve_user_path(argument)) / fs::path(from).filename();
        } else {
            to = explorer_form(resolve_user_path(argument));
        }
        to = explorer_form(to.lexically_normal());
        const std::string destination = to.string();
        if (destination == from) {
            return {false, "That is its name already"};
        }
        if (is_or_under(destination, from)) {
            return {false, "A folder can't be moved into itself"};
        }
        if (workspace.find(destination)) {
            return {false, destination + " is open in another tab"};
        }

        std::error_code ec;
        const bool on_disk = fs::exists(fs::symlink_status(from, ec));
        if (on_disk) {
            std::string folder_error;
            if (!create_parent_folders(to, folder_error)) {
                return {false, "Could not create " + folder_error};
            }
            const IoResult result = rename_path(from, to);
            if (!result.ok) {
                return {false, "Could not rename: " + result.error};
            }
        } else if (!workspace.find(from)) {
            return {false, "No such file: " + from};
        }
        // the open files (and their recovery copies, kept by path) follow.
        for (const auto& document : workspace.documents()) {
            if (is_or_under(document->path(), from)) {
                drop_recovery_copy(*document);
            }
        }
        workspace.rename_paths(from, destination);
        reveal_in_explorer(to);
        call(hooks.redraw);
        return {true, "Renamed " + from + " to " + destination};
    }

    // ">rm": to the trash, where it can be restored from. ">rm!": for good.
    CommandResult remove_command(const std::string& argument, bool for_good) {
        const std::string path = argument.empty() ? command_target()
                                                  : explorer_form(resolve_user_path(argument).lexically_normal()).string();
        if (path.empty()) {
            return {false, "Nothing to delete: open a file, or select one in the explorer"};
        }
        std::error_code ec;
        if (!fs::exists(fs::symlink_status(path, ec))) {
            return {false, "No such file or folder: " + path};
        }
        if (holds_opened_folder(path)) {
            return {false, "The opened folder can't be deleted from inside Itahi"};
        }
        for (const auto& document : workspace.documents()) {
            if (is_or_under(document->path(), path) && document->unsaved()) {
                return {false, document->path() + " has unsaved changes: save it (>w) or close it (>bd!) first"};
            }
        }

        const IoResult result = for_good ? delete_for_good(path) : move_to_trash(path);
        if (!result.ok) {
            if (for_good) {
                return {false, "Could not delete " + result.error + (fs::is_directory(path, ec) ? " (>rm moves it to the trash with what is in it)" : "")};
            }
            return {false, "Could not move it to the trash: " + result.error + " (>rm!" + (argument.empty() ? "" : " " + argument) + " deletes it for good)"};
        }

        // its tabs close, and the explorer forgets the folders that were open in it.
        std::vector<Document*> gone;
        for (const auto& document : workspace.documents()) {
            if (is_or_under(document->path(), path)) {
                gone.push_back(document.get());
            }
        }
        for (Document* document : gone) {
            drop_recovery_copy(*document);
            workspace.close(document);
        }
        for (auto folder = workspace.opened_folders.begin(); folder != workspace.opened_folders.end();) {
            folder = is_or_under(*folder, path) ? workspace.opened_folders.erase(folder) : std::next(folder);
        }
        call(hooks.refresh_explorer);
        call(hooks.refresh_git);
        call(hooks.redraw);
        return {true, (for_good ? "Deleted " : "Moved to the trash: ") + path};
    }

    // --- editing -------------------------------------------------------------

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
        std::size_t first_line = 0;
        std::size_t last_line = document->line_count() - 1;
        if (!whole_file) {
            first_line = last_line = document->line_of(cursor);
        }
        const std::size_t begin = document->line_start(first_line);
        const std::size_t end = last_line + 1 < document->line_count() ? document->line_start(last_line + 1) - 1 : content.size();

        // a pattern can need a step for every character it matches, and the
        // standard library takes them on the stack: on a very long line that
        // is a crash, with every unsaved file open. Such lines are left alone.
        constexpr std::size_t longest_line = 20000;
        std::string replaced;
        replaced.reserve(end - begin);
        std::size_t count = 0;
        std::size_t skipped = 0;
        try {
            const std::regex re(old_text, std::regex::ECMAScript);
            const auto flags = global ? std::regex_constants::format_default : std::regex_constants::format_first_only;
            // line by line: without "g" only the first match of each line changes (like vim).
            for (std::size_t line = first_line; line <= last_line; ++line) {
                const std::size_t line_begin = document->line_start(line);
                const std::size_t line_end = line + 1 < document->line_count() ? document->line_start(line + 1) - 1 : content.size();
                const auto from = content.begin() + static_cast<std::ptrdiff_t>(line_begin);
                const auto to = content.begin() + static_cast<std::ptrdiff_t>(line_end);
                if (line_end - line_begin > longest_line) {
                    ++skipped;
                    replaced.append(from, to);
                } else {
                    const std::size_t matches = static_cast<std::size_t>(std::distance(std::sregex_iterator(from, to, re), std::sregex_iterator()));
                    if (matches == 0) {
                        replaced.append(from, to);
                    } else {
                        count += global ? matches : 1;
                        std::regex_replace(std::back_inserter(replaced), from, to, re, new_text, flags);
                    }
                }
                if (line != last_line) {
                    replaced += '\n';
                }
            }
        } catch (const std::regex_error& error) {
            return {false, std::string("Invalid pattern: ") + error.what()};
        }
        const std::string skipped_note = skipped == 0 ? ""
            : " (" + std::to_string(skipped) + (skipped == 1 ? " line" : " lines") + " over " + std::to_string(longest_line) + " characters left as they were)";
        if (count == 0) {
            return {false, "Pattern not found: " + old_text + skipped_note};
        }

        // one undo step; the cursor stays where it was.
        document->replace(begin, end, replaced, false);
        document->set_cursor(cursor);
        return {true, "Substituted " + std::to_string(count) + (count == 1 ? " match" : " matches") + skipped_note};
    }

    // --- open files (tabs) -----------------------------------------------------

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
        call(hooks.show_editor);
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

    // --- windows ---------------------------------------------------------------

    std::optional<Side> side_called(const std::string& word) {
        if (word == "left" || word == "h") return Side::Left;
        if (word == "right" || word == "l") return Side::Right;
        if (word == "up" || word == "k") return Side::Up;
        if (word == "down" || word == "j") return Side::Down;
        return std::nullopt;
    }

    std::string window_position() {
        return "Window " + std::to_string(workspace.window_number(workspace.active_window())) + " of " +
               std::to_string(workspace.window_count());
    }

    // ">vsplit [file]" / ">split [file]": a new window beside or below the
    // active one, showing the same file or the one named.
    CommandResult split_window(Side side, const std::string& file) {
        std::string note;
        const int opened = workspace.split(side, note);
        if (opened < 0) {
            return {false, note};
        }
        if (!file.empty()) {
            const CommandResult shown = open_or_create(file);
            if (!shown.ok) {
                // no file to show there: no window either.
                std::string error;
                workspace.close_window(opened, error);
                return shown;
            }
        }
        call(hooks.show_editor);
        call(hooks.redraw);
        return {true, note.empty() ? window_position() : note};
    }

    CommandResult close_window_command(const std::string&, bool) {
        std::string error;
        if (!workspace.close_window(workspace.active_window(), error)) {
            return {false, error + " (>q quits, >bd closes the file)"};
        }
        call(hooks.show_editor);
        call(hooks.redraw);
        return {true, window_position()};
    }

    CommandResult only_window_command(const std::string&, bool) {
        if (workspace.window_count() < 2) {
            return {false, "Only one window is open"};
        }
        workspace.close_other_windows();
        call(hooks.show_editor);
        call(hooks.redraw);
        return {true, "The other windows are closed (their files are still open)"};
    }

    // ">win": which window this is. ">win 2", ">win left", ">win next": go to one.
    CommandResult window_command(const std::string& argument, bool) {
        if (argument.empty()) {
            const Document* document = workspace.active();
            return {true, window_position() + (document ? ": " + document->path() : "")};
        }
        if (argument == "next" || argument == "prev" || argument == "previous") {
            if (workspace.window_count() < 2) {
                return {false, "Only one window is open (>vsplit and >split open more)"};
            }
            workspace.focus_next(argument == "next" ? 1 : -1);
        } else if (const std::optional<Side> side = side_called(argument)) {
            if (!workspace.focus_side(*side)) {
                return {false, "No window on that side"};
            }
        } else if (argument.size() == 1 && std::isdigit(static_cast<unsigned char>(argument[0]))) {
            const int window = workspace.window_by_number(argument[0] - '0');
            if (window < 0) {
                return {false, "There is no window " + argument + " (" + std::to_string(workspace.window_count()) + " open)"};
            }
            workspace.focus_window(window);
        } else {
            return {false, "Usage: >win 1-4, or >win left / right / up / down / next / prev"};
        }
        call(hooks.show_editor);
        call(hooks.redraw);
        return {true, window_position()};
    }

    CommandResult move_window_command(const std::string& argument, bool) {
        const std::optional<Side> side = side_called(argument);
        if (!side) {
            return {false, "Usage: >wmove left / right / up / down"};
        }
        std::string error;
        if (!workspace.move_window(*side, error)) {
            return {false, error};
        }
        call(hooks.show_editor);
        call(hooks.redraw);
        return {true, window_position()};
    }

    // --- recovery --------------------------------------------------------------

    // ">recover": bring back the unsaved changes kept from a run that ended
    // badly. ">recover all": also the ones of other folders. ">recover!":
    // forget them instead.
    CommandResult recover_command(const std::string& argument, bool forget) {
        if (!argument.empty() && argument != "all") {
            return {false, "Usage: >recover, >recover all, or >recover! to forget the kept changes"};
        }
        std::error_code ec;
        const std::string root = absolute_normal(workspace.root);
        int done = 0;
        int elsewhere = 0;
        std::string problem;
        for (const RecoveryCopy& copy : recovery_copies()) {
            if (argument != "all" && !is_or_under(copy.path, root)) {
                ++elsewhere;
                continue;
            }
            if (forget) {
                fs::remove(copy.file, ec);
                ++done;
                continue;
            }
            std::string error;
            const std::optional<std::string> text = read_recovery_copy(copy, error);
            Document* document = text ? workspace.open(explorer_form(copy.path), error) : nullptr;
            if (!document) {
                problem = error;
                continue;
            }
            if (document->text() != *text) {
                // on top of what is on disk, as one step: "u" shows the file again.
                document->replace(0, document->text().size(), *text, false);
                document->set_cursor(0);
            }
            document->recovery_version = document->version();  // the copy on disk is this text
            ++done;
        }
        const std::string others = elsewhere == 0 ? ""
            : " (" + std::to_string(elsewhere) + " more from other folders: >recover all)";
        if (done == 0) {
            return {problem.empty(), problem.empty() ? "No unsaved changes were kept for this folder" + others : problem};
        }
        const std::string files = std::to_string(done) + (done == 1 ? " file" : " files");
        if (forget) {
            return {true, "Forgot the kept changes of " + files + others};
        }
        call(hooks.show_editor);
        call(hooks.redraw);
        return {true, "Brought back the unsaved changes of " + files + ": >w saves, u undoes" + others};
    }

    CommandResult help_command(const std::string&, bool) {
        call(hooks.show_help);
        return {true, hooks.show_help ? "" : "The keys and commands are listed in README.md"};
    }

    // --- the table ---------------------------------------------------------------

    enum class Argument { None, Optional, Required };

    struct EditorCommand {
        const char* names;  // the name, then its short forms: "vsplit vs"
        const char* usage;  // how it is typed, for the help screen
        const char* group;
        const char* help;
        Argument argument;   // a file name (or another word) after the command
        bool accepts_force;  // whether a '!' may follow the name
        CommandResult (*run)(const std::string& argument, bool force);
    };

    // every command the command line understands, in the order the help
    // screen shows them. A name only matches as a whole: "wq" is a command of
    // its own, "qw" is not one.
    const std::vector<EditorCommand>& editor_commands() {
        static const std::vector<EditorCommand> commands = {
            {"w", "w  ·  w name", "Files", "Save the open file; with a name, save it as that file (w! overwrites)", Argument::Optional, true, write_command},
            {"wa", "wa", "Files", "Save every unsaved file", Argument::None, true, write_all_command},
            {"e", "e name  ·  e", "Files", "Open a file, creating it and its folders when new; without a name, read the open file again (e! drops unsaved changes)", Argument::Optional, true, edit_command},
            {"q", "q", "Files", "Quit (refused while a file has unsaved changes; q! quits anyway)", Argument::None, true, quit_command},
            {"wq", "wq", "Files", "Save, then quit", Argument::None, true, write_quit_command},
            {"recover", "recover", "Files", "Bring back unsaved changes kept from a run that ended badly (recover! forgets them)", Argument::Optional, true, recover_command},

            {"bn", "bn  ·  bp", "Open files", "Show the next / previous open file", Argument::None, false, [](const std::string&, bool) { return switch_file(+1); }},
            {"bp", "", "Open files", "", Argument::None, false, [](const std::string&, bool) { return switch_file(-1); }},
            {"b", "b name", "Open files", "Show the open file called name (or the only one starting with it)", Argument::Required, false, show_open_file},
            {"bd", "bd", "Open files", "Close the open file (bd! even with unsaved changes)", Argument::None, true, [](const std::string&, bool force) { return close_active_file(force); }},
            {"ls", "ls", "Open files", "List the open files", Argument::None, false, list_open_files},

            {"vsplit vs", "vsplit  ·  vs file", "Windows", "Open a window on the right, with the same file or the one named", Argument::Optional, false, [](const std::string& file, bool) { return split_window(Side::Right, file); }},
            {"split sp", "split  ·  sp file", "Windows", "Open a window below", Argument::Optional, false, [](const std::string& file, bool) { return split_window(Side::Down, file); }},
            {"close clo", "close", "Windows", "Close the window (its file stays open)", Argument::None, false, close_window_command},
            {"only on", "only", "Windows", "Close every other window", Argument::None, false, only_window_command},
            {"win", "win 2  ·  win left", "Windows", "Go to a window: its number, or left / right / up / down / next / prev", Argument::Optional, false, window_command},
            {"wmove wm", "wmove right", "Windows", "Move the window left / right / up / down: it changes places with the one there, or joins the other column", Argument::Required, false, move_window_command},

            {"mkdir", "mkdir name", "Explorer", "Create a folder", Argument::Required, false, make_folder_command},
            {"rename mv", "rename name", "Explorer", "Rename the file or folder selected in the explorer (else the open file); a name with / moves it", Argument::Required, false, rename_command},
            {"rm", "rm  ·  rm name", "Explorer", "Move it to the trash (rm! deletes a file, or an empty folder, for good)", Argument::Optional, true, remove_command},
            {"refresh", "refresh", "Explorer", "Read the folders, the open files and the Git status again (F5)", Argument::None, false, refresh_command},

            {"u undo", "u  ·  red", "Editing", "Undo / redo", Argument::None, false, [](const std::string&, bool) { return undo_edit(); }},
            {"red redo", "", "Editing", "", Argument::None, false, [](const std::string&, bool) { return redo_edit(); }},

            {"help h", "help", "Help", "Show this list (F1)", Argument::None, false, help_command},
        };
        return commands;
    }

    const EditorCommand* find_command(const std::string& name) {
        static const std::map<std::string, const EditorCommand*> by_name = [] {
            std::map<std::string, const EditorCommand*> names;
            for (const EditorCommand& command : editor_commands()) {
                const std::string all = command.names;
                std::size_t start = 0;
                while (start < all.size()) {
                    std::size_t end = all.find(' ', start);
                    if (end == std::string::npos) {
                        end = all.size();
                    }
                    names[all.substr(start, end - start)] = &command;
                    start = end + 1;
                }
            }
            return names;
        }();
        const auto found = by_name.find(name);
        return found == by_name.end() ? nullptr : found->second;
    }

}  // namespace

const std::vector<CommandHelp>& command_help() {
    static const std::vector<CommandHelp> help = [] {
        std::vector<CommandHelp> rows;
        for (const EditorCommand& command : editor_commands()) {
            if (command.usage[0] != '\0') {
                rows.push_back({command.group, command.usage, command.help});
            }
        }
        // the ones that aren't a name: they are parsed on their own.
        rows.push_back({"Editing", "/text  ·  ?text", "Search down / up; every match is highlighted while typing (text\\c: any case)"});
        rows.push_back({"Editing", "s/old/new/", "Replace on the cursor's line (a g at the end: every match on the line)"});
        rows.push_back({"Editing", "%s/old/new/g", "Replace in the whole file"});
        rows.push_back({"Editing", "42", "Go to line 42"});
        std::stable_sort(rows.begin(), rows.end(), [](const CommandHelp& a, const CommandHelp& b) {
            static const std::vector<std::string> order = {"Files", "Open files", "Windows", "Explorer", "Editing", "Help"};
            return std::find(order.begin(), order.end(), a.group) < std::find(order.begin(), order.end(), b.group);
        });
        return rows;
    }();
    return help;
}

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
    drop_recovery_copy(*document);  // closed on purpose: nothing to offer back
    workspace.close(document);
    call(hooks.redraw);
    return {true, ""};
}

CommandResult switch_file(int step) {
    if (!workspace.cycle(step)) {
        return {false, workspace.documents().empty() ? "No file is open" : "Only one file is open"};
    }
    call(hooks.show_editor);
    return {true, ""};  // the tab bar shows which file it is
}

CommandResult reload_document(Document& document, bool force) {
    const DiskStamp now = disk_stamp(document.path());
    if (!now.exists) {
        return {false, document.path() + " isn't on disk" + (document.exists_on_disk() ? " any more (>w! writes it again)" : " yet (>w writes it)")};
    }
    if (!force && document.unsaved()) {
        return {false, "Unsaved changes in " + document.path() + " (>e! reads the file again and drops them)"};
    }
    std::string error;
    const std::optional<std::string> text = read_text_file(document.path(), error);
    if (!text) {
        return {false, error};
    }
    document.reload(*text);
    document.disk = now;
    document.disk_seen.reset();
    drop_recovery_copy(document);
    call(hooks.redraw);
    return {true, "Read " + document.path() + " again from disk"};
}

std::string check_files_on_disk() {
    std::string message;
    for (const auto& document : workspace.documents()) {
        const DiskStamp now = disk_stamp(document->path());
        if (now == document->disk || (document->disk_seen && now == *document->disk_seen)) {
            continue;  // as it was, or already said
        }
        if (!now.exists) {
            document->disk_seen = now;
            message = document->path() + " was deleted on disk (>w! writes it again)";
        } else if (!document->unsaved() && settings.auto_reload) {
            const CommandResult result = reload_document(*document, false);
            if (!result.ok) {
                document->disk_seen = now;
            }
            message = result.ok ? "Read " + document->path() + " again: it changed on disk" : result.message;
        } else {
            document->disk_seen = now;
            message = document->path() + " changed on disk (" +
                      (document->unsaved() ? ">e! reads it again and drops your changes, >w! overwrites it" : ">e reads it again") + ")";
        }
    }
    return message;
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
        if (found.ok) {
            call(hooks.show_editor);
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

    const EditorCommand* action = name.empty() ? nullptr : find_command(name);
    if (!action) {
        return {false, "Not an editor command: " + command + " (>help lists them)"};
    }

    const bool force = !rest.empty() && rest[0] == '!';
    if (force) {
        rest.erase(0, 1);
    }
    if (!rest.empty() && rest[0] != ' ') {
        return {false, "Trailing characters: " + rest};
    }
    const std::string argument = trim(rest);

    if (force && !action->accepts_force) {
        return {false, "No ! allowed: " + command};
    }
    if (!argument.empty() && action->argument == Argument::None) {
        return {false, "Trailing characters: " + argument};
    }
    if (argument.empty() && action->argument == Argument::Required) {
        const std::string what = name == "b" ? " name of an open file" : name == "wmove" || name == "wm" ? " left / right / up / down" : " name";
        return {false, "Argument required: >" + name + what};
    }
    return action->run(argument, force);
}
