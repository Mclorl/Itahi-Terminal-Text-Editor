
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <utility>

#include "../library/custom_styling.hpp"
#include "../library/global_variables.hpp"

namespace {

    // decorator recording the exact box given by the parent during layout. Unlike
    // `ftxui::reflect`, Render() does not intersect the recorded box with the
    // screen stencil, so off-screen / larger-than-viewport content keeps a valid
    // box.
    class BoxCapture : public ftxui::Node {
    public:
    BoxCapture(ftxui::Element child, ftxui::Box& box) : ftxui::Node({std::move(child)}), captured_box_(box) {}

    void ComputeRequirement() override {
        ftxui::Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
    }

    void SetBox(ftxui::Box box) override {
        captured_box_ = box;
        ftxui::Node::SetBox(box);
        children_[0]->SetBox(box);
    }

    private:
    ftxui::Box& captured_box_;
    };

    // single line inputs: Up/Down are not given to the Input (it would use
    // them to jump to the start/end of the text) so the parent container moves
    // to the previous/next row in one key press.
    class PassVerticalArrows : public ftxui::ComponentBase {
    public:
    explicit PassVerticalArrows(ftxui::Component child) { Add(std::move(child)); }

    bool OnEvent(ftxui::Event event) override {
        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
            return false;
        }
        return ftxui::ComponentBase::OnEvent(event);
    }
    };

} // namespace last

ftxui::Decorator capture_box(ftxui::Box& box) {
    return [&box](ftxui::Element child) -> ftxui::Element {
        return std::make_shared<BoxCapture>(std::move(child), box);
    };
}


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
    ftxui::Component input_num = ftxui::Input(&target_var, "");

    // the label without its trailing ": " names the setting in the errors.
    const std::string setting_name = label_text.substr(0, label_text.find_last_not_of(": ") + 1);

    input_num |= ftxui::CatchEvent([&target_var, max_digits_taken, setting_name](ftxui::Event event) {

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
                command_error = setting_name + " only accepts numbers";
                return true;
            }
            // Stop accepting text inputs if the max_digits is reached
            if (target_var.size() >= max_digits_taken) {
                command_error = setting_name + " accepts at most " + std::to_string(max_digits_taken) + " digits";
                return true;
            }
        }
        return false;
    });

    input_num = std::make_shared<PassVerticalArrows>(input_num);

    return ftxui::Renderer(input_num, [input_num, label_text, max_digits_taken, max_width]() {
        return ftxui::hbox({
            ftxui::text(label_text),
            ftxui::underlined(
                ftxui::size(ftxui::WIDTH, ftxui::EQUAL, max_width)(input_num->Render())
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

