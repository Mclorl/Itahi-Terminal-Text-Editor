#include "ui/command_line.hpp"

#include <algorithm>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "editor/commands.hpp"
#include "editor/paths.hpp"
#include "editor/search.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"

std::function<void(const std::string&)> open_command_line;

namespace {

    // the commands whose argument is a path: Tab completes it.
    const char* const path_commands[] = {">e ", ">w! ", ">w ", ">vsplit ", ">vs ", ">split ", ">sp ",
                                         ">mkdir ", ">rename ", ">mv ", ">rm! ", ">rm "};

    // where the path starts in `line`, npos when its command takes none.
    std::size_t path_start(const std::string& line) {
        for (const char* prefix : path_commands) {
            if (line.rfind(prefix, 0) == 0) {
                const std::size_t first = line.find_first_not_of(' ', std::string(prefix).size());
                return first == std::string::npos ? line.size() : first;
            }
        }
        return std::string::npos;
    }

    // what >rename and >rm act on when no path is typed.
    std::string target_name() {
        const std::string selected = hooks.command_target ? hooks.command_target() : std::string();
        const std::string target = selected.empty() ? active_path() : selected;
        return target.empty() ? "nothing (no file is open)" : target;
    }

    // which command of the history is shown (its size: none, a new line).
    std::size_t history_at = 0;
    std::string history_draft;  // what was typed before Up was pressed

}  // namespace

ftxui::Element command_line_area(const ftxui::Component& input) {
    auto error_line = [] {
        return ftxui::color(ftxui::Color::White, ftxui::bgcolor(ftxui::Color::Red, ftxui::text(command_error)));
    };

    if (mode != Mode::Command) {
        if (!command_error.empty()) {
            return error_line();
        }
        if (!command_output.empty()) {
            return ftxui::dim(ftxui::text(command_output));
        }
        return ftxui::emptyElement();
    }

    ftxui::Element line = input->Render();
    const auto hint = [&line](const std::string& text) {
        line = ftxui::hbox({line | ftxui::flex, ftxui::dim(ftxui::text(" " + text + " "))});
    };
    // a command that makes or opens a file, with a plain name: show the
    // folder it will go to.
    if (const std::size_t start = path_start(command_line); start != std::string::npos) {
        const std::string name = command_line.substr(start);
        const bool renames = command_line.rfind(">rename ", 0) == 0 || command_line.rfind(">mv ", 0) == 0;
        const bool removes = command_line.rfind(">rm", 0) == 0;
        if (renames) {
            hint("renames " + target_name() + "  Enter: rename  Esc: cancel");
        } else if (removes) {
            hint(std::string(command_line.rfind(">rm!", 0) == 0 ? "deletes " : "moves ") + (name.empty() ? target_name() : name) +
                 (command_line.rfind(">rm!", 0) == 0 ? " for good" : " to the trash") + "  Enter: confirm  Esc: cancel");
        } else if (!name.empty() && name.back() != '/') {
            // the folder the file will really be in, for what is typed now.
            std::string folder = resolve_user_path(name).parent_path().lexically_normal().string();
            if (folder.empty() || folder == ".") {
                folder = "the opened folder";
            } else if (folder.back() != '/') {
                folder += '/';
            }
            hint("in " + folder + "  Tab: complete  Enter: create / open");
        }
    } else if (command_line == ">rm" || command_line == ">rm!") {
        hint(std::string(command_line == ">rm!" ? "deletes " : "moves ") + target_name() +
             (command_line == ">rm!" ? " for good" : " to the trash") + "  Enter: confirm  Esc: cancel");
    }

    // a search shows where it is: "2 of 5", like vscode.
    const SearchState& search = search_state();
    if (search.active && !search.pattern.empty()) {
        const std::string count = search.matches.empty() ? "No results"
            : search.current < 0 ? std::to_string(search.matches.size()) + (search.matches.size() == 1 ? " match" : " matches")
            : std::to_string(search.current + 1) + " of " + std::to_string(search.matches.size());
        const std::string options = std::string(search.ignore_case ? "  [any case]" : "") + (search.whole_word ? "  [whole words]" : "");
        hint(count + options + "  Enter/Down: next  Up: previous  Alt+C: case  Alt+W: words  Esc: close");
    }

    // Tab's options: on a gray strip under the command line, every name,
    // wrapped across as many rows as needed.
    ftxui::Element options = ftxui::emptyElement();
    if (!command_completions.empty()) {
        ftxui::Elements names;
        for (const std::string& name : command_completions) {
            names.push_back(ftxui::text(name + "   "));
        }
        options = ftxui::color(ftxui::Color::White,
            ftxui::bgcolor(ftxui::Color::GrayDark, ftxui::flexbox(std::move(names)) | ftxui::xflex));
    }
    if (command_error.empty()) {
        return ftxui::vbox({line, options});
    }
    return ftxui::vbox({error_line(), line, options});
}

std::string command_line_ghost() {
    // only at the end of a command that makes a file or a folder, before any
    // name is typed.
    if (command_cursor_index != static_cast<int>(command_line.size())) {
        return "";
    }
    for (const char* command : {">e", ">mkdir", ">vsplit", ">vs", ">split", ">sp"}) {
        const std::string name = command;
        // ">e" alone, or with its space: the split commands only with the
        // space (alone they split without a file).
        const bool alone_too = name == ">e" || name == ">mkdir";
        if (command_line == name + " " || (alone_too && command_line == name)) {
            const std::string folder = active_folder_for_command();
            if (folder.empty()) {
                return "";
            }
            return command_line == name ? " " + folder : folder;
        }
    }
    return "";
}

ftxui::Component command_line_input(std::function<void()> on_leave) {
    ftxui::InputOption option;
    option.multiline = false;
    option.cursor_position = &command_cursor_index;
    // the ghost text sits right after the cursor, in gray: a suggestion only,
    // not part of the command until Tab accepts it.
    option.transform = [](ftxui::InputState state) {
        const std::string ghost = command_line_ghost();
        if (ghost.empty() || !state.focused) {
            return state.element;
        }
        return ftxui::hbox({state.element, ftxui::color(ftxui::Color::GrayDark, ftxui::text(ghost))});
    };

    ftxui::Component input = ftxui::Input(&command_line, "", option);

    // raw pointer for the same reason as in `file_content_input`.
    ftxui::ComponentBase* input_ptr = input.get();

    // closing the command line also ends a search (no more highlights).
    auto leave = [on_leave] {
        search_close();
        on_leave();
    };

    // ">/text" or ">?text": a search, live while it is typed.
    auto is_search = [] {
        return command_line.size() >= 2 && (command_line[1] == '/' || command_line[1] == '?');
    };

    return ftxui::CatchEvent(input, [input_ptr, leave, is_search](ftxui::Event event) {
        if (event.is_mouse()) {
            return false;
        }

        if (event == ftxui::Event::Escape) {
            history_at = command_history.size();
            leave();
            return true;
        }

        // while searching: Alt+C matches any case, Alt+W whole words only.
        if (is_search() && (event == ftxui::Event::AltC || event == ftxui::Event::AltW)) {
            bool& toggled = event == ftxui::Event::AltC ? settings.search_ignore_case : settings.search_whole_word;
            toggled = !toggled;
            search_preview(command_line.substr(2), command_line[1] == '?');
            return true;
        }

        // Up / Down: the commands run before, like a shell.
        if (!is_search() && (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown)) {
            history_at = std::min(history_at, command_history.size());
            if (event == ftxui::Event::ArrowUp && history_at > 0) {
                if (history_at == command_history.size()) {
                    history_draft = command_line;
                }
                command_line = ">" + command_history[--history_at];
            } else if (event == ftxui::Event::ArrowDown && history_at < command_history.size()) {
                ++history_at;
                command_line = history_at == command_history.size() ? history_draft : ">" + command_history[history_at];
            }
            command_cursor_index = static_cast<int>(command_line.size());
            command_completions.clear();
            return true;
        }

        // the options listed by the last Tab stay only until the next key.
        command_completions.clear();

        // Tab with a ghost shown: take the suggested folder.
        if (event == ftxui::Event::Tab && !command_line_ghost().empty()) {
            command_line += command_line_ghost();
            command_cursor_index = static_cast<int>(command_line.size());
            return true;
        }

        // Tab completes the file name of the commands that take one, like a shell's cd.
        if (event == ftxui::Event::Tab) {
            const std::size_t start = path_start(command_line);
            if (start == std::string::npos) {
                return true;  // nothing to complete
            }
            const PathCompletion completion = complete_path(command_line.substr(start));
            command_line = command_line.substr(0, start) + completion.text;
            command_cursor_index = static_cast<int>(command_line.size());
            command_completions = completion.options;
            return true;
        }

        // search: Enter (or Down) goes to the next match and keeps the search
        // open, so pressing it again walks through every match; Up goes back.
        if (is_search() && (event == ftxui::Event::Return || event == ftxui::Event::ArrowDown ||
                            event == ftxui::Event::ArrowUp)) {
            const CommandResult result = search_step(event == ftxui::Event::ArrowUp);
            if (result.ok) {
                command_error.clear();
            } else {
                command_error = result.message;
            }
            return true;
        }

        if (event == ftxui::Event::Return) {
            remember_command(command_line.substr(1));
            history_at = command_history.size();
            const CommandResult result = run_command(command_line.substr(1)); // without the '>' prompt

            // an invalid or refused command keeps the command line open, with
            // the error above it, so it can be fixed.
            if (!result.ok) {
                command_error = result.message;
                return true;
            }
            leave();
            command_output = result.message;
            return true;
        }

        // same Backspace mapping as edit mode (see file_content_input).
        const ftxui::Event key = event == ftxui::Event::CtrlH ? ftxui::Event::Backspace : event;

        // only editing and left/right keys reach the Input. Everything else
        // (Up/Down, Tab, ...) is swallowed, otherwise the containers would move
        // the focus out of the command line.
        if (key.is_character() || key == ftxui::Event::Backspace || key == ftxui::Event::Delete ||
            key == ftxui::Event::ArrowLeft || key == ftxui::Event::ArrowRight ||
            key == ftxui::Event::Home || key == ftxui::Event::End) {
            input_ptr->OnEvent(key);
        }

        // the '>' prompt was deleted: close the command line.
        if (command_line.empty() || command_line[0] != '>') {
            leave();
            return true;
        }

        // highlight every match of what is typed so far.
        if (is_search()) {
            search_preview(command_line.substr(2), command_line[1] == '?');
            command_error.clear();
        } else if (search_state().active) {
            search_close();
        }

        // the cursor never goes before the prompt, so only a Backspace right
        // after it can delete it.
        command_cursor_index = std::max(1, command_cursor_index);
        return true;
    });
}
