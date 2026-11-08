#include "ui/command_line.hpp"

#include <algorithm>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "editor/commands.hpp"
#include "editor/paths.hpp"
#include "editor/search.hpp"
#include "editor/state.hpp"

std::function<void(const std::string&)> open_command_line;

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
    // >e / >w with a plain name: show the folder it will go to.
    for (const char* prefix : {">e ", ">w! ", ">w "}) {
        if (command_line.rfind(prefix, 0) == 0) {
            const std::string name = command_line.substr(std::string(prefix).size());
            // the folder the file will really be in, for what is typed now.
            if (!name.empty() && name.back() != '/') {
                std::string folder = resolve_user_path(name).parent_path().lexically_normal().string();
                if (folder.empty() || folder == ".") {
                    folder = "the opened folder";
                } else if (folder.back() != '/') {
                    folder += '/';
                }
                line = ftxui::hbox({line | ftxui::flex, ftxui::dim(ftxui::text(" in " + folder + "  Tab: complete  Enter: create / open "))});
            }
            break;
        }
    }

    // a search shows where it is: "2 of 5", like vscode.
    const SearchState& search = search_state();
    if (search.active && !search.pattern.empty()) {
        const std::string count = search.matches.empty() ? "No results"
            : search.current < 0 ? std::to_string(search.matches.size()) + (search.matches.size() == 1 ? " match" : " matches")
            : std::to_string(search.current + 1) + " of " + std::to_string(search.matches.size());
        line = ftxui::hbox({line | ftxui::flex, ftxui::dim(ftxui::text(" " + count + "  Enter/Down: next  Up: previous  Esc: close "))});
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
    // only at the end of ">e" or ">e " (no file name typed yet).
    if (command_cursor_index != static_cast<int>(command_line.size())) {
        return "";
    }
    if (command_line != ">e" && command_line != ">e ") {
        return "";
    }
    const std::string folder = active_folder_for_command();
    if (folder.empty()) {
        return "";
    }
    return command_line == ">e" ? " " + folder : folder;
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
            leave();
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

        // Tab completes the file name of >e, >w and >w!, like a shell's cd.
        if (event == ftxui::Event::Tab) {
            std::size_t start = std::string::npos;
            for (const char* prefix : {">e ", ">w! ", ">w "}) {
                if (command_line.rfind(prefix, 0) == 0) {
                    start = std::string(prefix).size();
                    break;
                }
            }
            if (start == std::string::npos) {
                return true;  // nothing to complete
            }
            const std::size_t first = command_line.find_first_not_of(' ', start);
            const std::size_t path_start = first == std::string::npos ? command_line.size() : first;
            const PathCompletion completion = complete_path(command_line.substr(path_start));
            command_line = command_line.substr(0, path_start) + completion.text;
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
