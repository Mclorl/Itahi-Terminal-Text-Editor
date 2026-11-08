#include "ui/editor_view.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/string.hpp>

#include "editor/mode.hpp"
#include "editor/settings.hpp"
#include "editor/search.hpp"
#include "editor/state.hpp"
#include "editor/undo_history.hpp"
#include "ui/ui_state.hpp"
#include "ui/wrapped_view.hpp"

namespace {

    // byte offset where the line holding byte `index` starts.
    std::size_t line_start_of(const std::string &text, int index) {
        if (index <= 0) {
            return 0;
        }
        const std::size_t newline = text.rfind('\n', static_cast<std::size_t>(index - 1));
        return newline == std::string::npos ? 0 : newline + 1;
    }

    // Tab: insert spaces up to the next tab stop, like other editors. Spaces,
    // not '\t': the editor can't display tab characters yet.
    void insert_indent(std::string &text, int &cursor) {
        const int size = get_tab_size();
        cursor = std::clamp(cursor, 0, static_cast<int>(text.size()));

        const std::size_t start = line_start_of(text, cursor);
        const int column = ftxui::string_width(text.substr(start, static_cast<std::size_t>(cursor) - start));
        const int spaces = size - column % size;

        text.insert(static_cast<std::size_t>(cursor), static_cast<std::size_t>(spaces), ' ');
        cursor += spaces;
    }

    // Shift+Tab: remove one indent level (a tab, or up to tab-size spaces)
    // from the start of the current line.
    void remove_indent(std::string &text, int &cursor) {
        const int size = get_tab_size();
        cursor = std::clamp(cursor, 0, static_cast<int>(text.size()));

        const std::size_t start = line_start_of(text, cursor);
        std::size_t count = 0;
        if (start < text.size() && text[start] == '\t') {
            count = 1;
        } else {
            while (count < static_cast<std::size_t>(size) && start + count < text.size() && text[start + count] == ' ') {
                count++;
            }
        }
        if (count == 0) {
            return;
        }

        text.erase(start, count);
        cursor = std::max(static_cast<int>(start), cursor - static_cast<int>(count));
    }

    // the patched FTXUI turns a raw LF (Ctrl+J, or a line break in pasted
    // text) into this sequence.
    const ftxui::Event ctrl_j = ftxui::Event::Special("\x1B[106;5u");

}  // namespace

// custom input styling function.

ftxui::InputOption style_file_content_input_option(int &cursor) {
    // input, main content
        ftxui::InputOption file_content_input_option_styling;

        file_content_input_option_styling.cursor_position = &cursor;
        file_content_input_option_styling.multiline = true; // explicitly enable multiline

        file_content_input_option_styling.transform = [](ftxui::InputState state) {
            if (state.is_placeholder) {
                return ftxui::color(ftxui::Color::GrayDark, state.element);
            }
            return state.element;
        };
        return file_content_input_option_styling;
}

ftxui::Component file_content_count_component(
    std::string &text,
    bool &numbers_on,
    int &cursor,
    bool &underline_active
) {
    return ftxui::Renderer([
        &text,
        &numbers_on,
        &cursor,
        &underline_active

    ]() {

        if (!numbers_on) {
            return ftxui::emptyElement();
        }

        const std::size_t total_lines = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;

        const std::size_t safe_cursor_limit = std::min(static_cast<std::size_t>(std::max(cursor, 0)), text.size());
        const std::size_t active_cursor_line = static_cast<std::size_t>(
            std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(safe_cursor_limit), '\n'));

        // right-aligned to the widest number (at least 4 columns).
        const std::size_t width = std::max<std::size_t>(4, std::to_string(total_lines).size());

        ftxui::Elements line_rows;
        for (std::size_t i = 0; i < total_lines; i++) {
            std::string num_str = std::to_string(i + 1);
            num_str = std::string(width - num_str.length(), ' ') + num_str;

            ftxui::Element single_row_element = ftxui::text(num_str);

            if (i == active_cursor_line) {
                // highlight the active line number (bright white/yellow and bold)
                single_row_element = underline_active ? ftxui::bold(ftxui::color(ftxui::Color::Yellow, ftxui::underlined(single_row_element))) : ftxui::bold(ftxui::color(ftxui::Color::Yellow, single_row_element));
            } else {
                // dim all inactive line numbers
                single_row_element = ftxui::dim(single_row_element);
            }

            line_rows.push_back(single_row_element);
        }

        return ftxui::vbox(std::move(line_rows));
    });
}

ftxui::Component file_content_input(std::string &text, int &cursor) {
    ftxui::InputOption custom_input_style_update = style_file_content_input_option(cursor);

    custom_input_style_update.on_change = [&text] { // no delay on updating the active_file
        refresh_unsaved_status(text);
    };

    ftxui::Component main_user_input = ftxui::Input(&text, "Type something...", custom_input_style_update);

    // raw pointer to the Input on purpose: capturing the shared_ptr inside its
    // own CatchEvent would create a reference cycle.
    ftxui::ComponentBase* input_ptr = main_user_input.get();

    return ftxui::CatchEvent(main_user_input, [&text, &cursor, input_ptr](ftxui::Event event) {
        // word wrap: the Input isn't drawn, so it can't tell where a click
        // landed. The wrapped view knows: place the cursor there.
        if (event.is_mouse() && word_wrap) {
            const ftxui::Mouse& mouse = event.mouse();
            if (mouse.button != ftxui::Mouse::Left || mouse.motion != ftxui::Mouse::Pressed) {
                return false;
            }
            const std::optional<std::size_t> byte = wrapped_byte_at(mouse.x, mouse.y);
            if (!byte) {
                return false;
            }
            cursor = static_cast<int>(*byte);
            input_ptr->TakeFocus();
            return true;
        }

        // never swallow mouse events: the Input needs them to take focus (and
        // to place the cursor). Swallowing them is why the cursor vanished.
        if (event.is_mouse()) {
            return false;
        }

        // at a boundary (first/last line, start/end of the buffer) the Input
        // can't move the cursor and reports the arrow as unhandled, so the
        // parent containers would move focus out of the editor (up -> top bar,
        // left -> explorer, ...). Always consume arrows, in every mode.
        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown ||
            event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight ||
            event == ftxui::Event::ArrowLeftCtrl || event == ftxui::Event::ArrowRightCtrl) {
            input_ptr->OnEvent(event);
            return true;
        }

        // Home / End: start / end of the current line (the Input jumps to
        // the start / end of the whole file).
        if (event == ftxui::Event::Home || event == ftxui::Event::End) {
            cursor = std::clamp(cursor, 0, static_cast<int>(text.size()));
            if (event == ftxui::Event::Home) {
                cursor = static_cast<int>(line_start_of(text, cursor));
            } else {
                const std::size_t newline = text.find('\n', static_cast<std::size_t>(cursor));
                cursor = static_cast<int>(newline == std::string::npos ? text.size() : newline);
            }
            return true;
        }

        // Page Up / Page Down: the Input ignores them, so move one screen of lines.
        if (event == ftxui::Event::PageUp || event == ftxui::Event::PageDown) {
            const ftxui::Event arrow = event == ftxui::Event::PageUp ? ftxui::Event::ArrowUp
                                                                     : ftxui::Event::ArrowDown;
            for (int i = 0; i < std::max(1, screen->dimy() - 4); i++) {
                input_ptr->OnEvent(arrow);
            }
            return true;
        }

        // --- Edit mode ---
        if (mode == Mode::Edit) {
            // Escape is handled for every panel in main.cpp.

            // some terminals send Ctrl+H (0x08) for the Backspace key. FTXUI is
            // patched to keep Ctrl+H separate, so map it back here while typing.
            if (event == ftxui::Event::CtrlH) {
                record_edit();
                input_ptr->OnEvent(ftxui::Event::Backspace);
                return true;
            }
            // a raw LF (pasted text) is a new line too.
            if (event == ctrl_j) {
                record_edit();
                input_ptr->OnEvent(ftxui::Event::Return);
                return true;
            }
            // Tab / Shift+Tab indent and unindent. Without this the Input
            // ignores them and the containers move the focus away instead.
            if (event == ftxui::Event::Tab) {
                record_edit();
                insert_indent(text, cursor);
                refresh_unsaved_status(text);
                return true;
            }
            if (event == ftxui::Event::TabReverse) {
                record_edit();
                remove_indent(text, cursor);
                refresh_unsaved_status(text);
                return true;
            }
            if (event.is_character() || event == ftxui::Event::Return ||
                event == ftxui::Event::Backspace || event == ftxui::Event::Delete) {
                record_edit();
            }
            return false;  // forwarded to the Input
        }

        // --- Normal mode ---

        // h/j/k/l act as arrow keys.
        if (event == ftxui::Event::Character('h')) { input_ptr->OnEvent(ftxui::Event::ArrowLeft);  return true; }
        if (event == ftxui::Event::Character('j')) { input_ptr->OnEvent(ftxui::Event::ArrowDown);  return true; }
        if (event == ftxui::Event::Character('k')) { input_ptr->OnEvent(ftxui::Event::ArrowUp);    return true; }
        if (event == ftxui::Event::Character('l')) { input_ptr->OnEvent(ftxui::Event::ArrowRight); return true; }

        // n / N repeat the last search, forward / backward (like vim).
        if (event == ftxui::Event::Character('n') || event == ftxui::Event::Character('N')) {
            const CommandResult result = search_again(event == ftxui::Event::Character('N'));
            (result.ok ? command_output : command_error) = result.message;
            return true;
        }

        // u undoes, Ctrl+R redoes (like vim).
        if (event == ftxui::Event::Character('u') || event == ftxui::Event::CtrlR) {
            const CommandResult result = event == ftxui::Event::CtrlR ? redo_edit() : undo_edit();
            (result.ok ? command_output : command_error) = result.message;
            return true;
        }

        // Enter Edit mode (e, or i like vim).
        if (event == ftxui::Event::Character('e') || event == ftxui::Event::Character('i')) {
            set_edit_mode();
            input_ptr->TakeFocus();  // keeps the cursor visible in the editor
            return true;
        }

        // Printable characters and editing keys are consumed so normal mode
        // can never modify the buffer.
        if (event.is_character() || event == ftxui::Event::Return || event == ftxui::Event::Backspace ||
            event == ftxui::Event::Delete || event == ctrl_j) {
            return true;
        }

        return false;
    });
}
