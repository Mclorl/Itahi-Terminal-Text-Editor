
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <unordered_set>

#include "../library/custom_styling.hpp"
#include "../library/global_variables.hpp"


// custom input styling variable.
int cursor_index = 0;

// custom input styling function.

ftxui::InputOption style_file_content_input_option(int &cursor_index) {
    // input, main content
        ftxui::InputOption file_content_input_option_styling;

        file_content_input_option_styling.cursor_position = &cursor_index;
        file_content_input_option_styling.multiline = true; // explicitly enable multiline

        file_content_input_option_styling.transform = [](ftxui::InputState state) {
            auto e = state.element;
            
            if (state.is_placeholder) {
                return ftxui::color(ftxui::Color::GrayDark, e);
            }

            if (state.focused){
                return e;
            }
            return e;
        };
        return file_content_input_option_styling;
}

ftxui::Component file_content_count_component(
    std::string &content,
    bool &show_line_numbers,
    int &cursor_index,
    bool &underline_active_row_on_numbers
) {
    return ftxui::Renderer([
        &content,
        &show_line_numbers,
        &cursor_index,
        &underline_active_row_on_numbers

    ]() {

        uint32_t total_lines = std::count(content.begin(), content.end(), '\n');
        if (!content.empty() || total_lines == 0) {
            total_lines++;
        }

        int active_cursor_line = 0;
        int safe_cursor_limit = std::min((size_t)cursor_index, content.size());
        for (int i = 0; i < safe_cursor_limit; i++) {
            if (content[i] == '\n') {
                active_cursor_line++;
            }
        }

        ftxui::Elements line_rows; // this is a vector under the FTXUI header files. So any vector functions can be used.
        for (int i = 0; i < total_lines; i++) {
            std::string num_str = show_line_numbers ? std::to_string(i + 1) : "|";
            
            // format to a clean fixed width of 4 characters or 1000+ up to 9999 lines
            if (num_str.length() < 4) {
                num_str = std::string(4 - num_str.length(), ' ') + num_str;
            }

            ftxui::Element single_row_element = ftxui::text(num_str);

            if (i == active_cursor_line) {
                // highlight the active line number (bright white/yellow and bold)
                single_row_element = underline_active_row_on_numbers ? ftxui::bold(ftxui::color(ftxui::Color::Yellow, ftxui::underlined(single_row_element))) : ftxui::bold(ftxui::color(ftxui::Color::Yellow, single_row_element));
            } else {
                // dim all inactive line numbers
                single_row_element = ftxui::dim(single_row_element);
            }

            line_rows.push_back(single_row_element);
        }

        return ftxui::vbox(std::move(line_rows));
    });
}
    

// left panel file styling

ftxui::ButtonOption left_panel_file_styling() {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();

    return option;
}

ftxui::ButtonOption left_panel_directory_styling() {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    // this is now in the file_system_left_panel.cpp. This is just a save for the default style directory.
    // option.transform = [path, &opened_folders](const ftxui::EntryState& current_state) {
    //     bool is_open = opened_folders.find(path) != opened_folders.end();
    //     std::string prefix_folder_status = is_open ? "v " : "> ";

    //     auto e = ftxui::text(prefix_folder_status + current_state.label);
            
    //     if (current_state.focused) {
    //         return ftxui::bold(e);
    //     }
    //     return e;
    // };

    return option;
}

// numeric input custom
ftxui::Component make_numeric_input_row(std::string& target_var, std::string label_text, int max_digits_taken, int max_width) {
    ftxui::Component input_comp = ftxui::Input(&target_var, "");

    input_comp |= ftxui::CatchEvent([&target_var, max_digits_taken](ftxui::Event event) {

        // Block Enter key 
        if (event == ftxui::Event::Return) {
            return true;
        }
        if (event.is_character()) {
            std::string input_str = event.character();

            if (input_str.empty()) {
                return false;
            }

            // blocks any character other than int
            if (!std::isdigit(static_cast<unsigned char>(event.character()[0]))) {
                return true;
            }
            // Stop accepting text inputs if the max_digits is reached
            if (target_var.size() >= max_digits_taken) {
                return true;
            }
        }
        return false;
    });

    return ftxui::Renderer(input_comp, [input_comp, label_text, max_digits_taken, max_width]() {
        return ftxui::hbox({
            ftxui::text(label_text),
            ftxui::underlined(
                ftxui::size(ftxui::WIDTH, ftxui::EQUAL, max_width)(input_comp->Render())
            )
        });
    });
}

ftxui::ButtonOption create_plain_button_option_symbol() {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.transform = [](const ftxui::EntryState& state) {
        auto e = ftxui::bold(ftxui::text(" " + state.label + " "));
        if (state.focused) {
            return ftxui::bgcolor(ftxui::Color::White, ftxui::color(ftxui::Color::Black, e));
        }
        return e;
    };

    return option;
}

ftxui::ButtonOption create_plain_button_option() {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();

    option.transform = [](const ftxui::EntryState& state) {
        auto e = ftxui::text("< " + state.label + " >");
        if (state.focused) {
            return ftxui::bold(e);
        }
        return e;
    };

    return option;
}

ftxui::CheckboxOption create_plain_checkbox_option() {
    ftxui::CheckboxOption option = ftxui::CheckboxOption::Simple();
    
    option.transform = [](const ftxui::EntryState& state) {
        std::string check = state.state ? "[X] " : "[ ] ";
        auto e = ftxui::text(check + state.label);
        if (state.focused) {
            return ftxui::bold(e);
        }
        return e;
    };

    return option;
}

// text button array (for tabs)
ftxui::Component create_text_button(std::string label, int tab_index, int &tab_selected, int &status) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        
    option.transform = [label, tab_index, &tab_selected](const ftxui::EntryState& state) {
        if (tab_selected == tab_index + 1) {
            return ftxui::bold(
                ftxui::text("[" + label + "]")
            );
        } 
        if (state.focused) {
            return ftxui::bold(
                ftxui::text(" " + label + " ")
            );
        }
        return ftxui::text(" " + label + " ");
    };

    return ftxui::Button(label, [tab_index, &tab_selected, &status]() {
        if (status == 1) {
            status = 2;
        }
        tab_selected = tab_index + 1;
    }, option);
}

