#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "app_data.hpp"
#include "file_system_left_panel.hpp"
#include "file_entry.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    auto screen = ftxui::ScreenInteractive::Fullscreen();

    int tab_selected = 0;      // 1: File, 2: Edit, 3: View, 4: *Terminal, 5: Help, 0 not active
    int left_panel_width = 25; // Default width of the left panel slider

    // error catch var

    std::error_code ec;

    // data 
    std::string content;

    std::string current_directory_content;
    

    // mouse sensitivity
    std::string mouse_sensitivity = "3";

    std::string active_file = "";

    // status
    int status = 1;

    // Interactive Toggles default
    // -- files -- 
    bool auto_save = false;
    // -- edit -- 
    // bool word_wrap = true;
    // -- view -- 
    bool show_line_numbers = true;
    bool underline_active_row_on_numbers = false;
    // bool show_status_bar = true;

    // custom input styling variable.
    int cursor_index = 0;

    // error handling
    if (ec) {
        std::cerr << "Failed to retrieve current path: " << ec.message() << std::endl;
        return 1;
    }

    if (argc > 1 && !fs::is_regular_file(fs::status(argv[1])) && !fs::is_directory(fs::status(argv[1]))) {
        std::cerr << std::endl << "There is no such file or directory as " << argv[1] << std::endl << "Abort." << std::endl;
        return 1;
    }

    if (argc > 2) {
        std::cerr << std::endl << "Too much arguments." << std::endl << "Abort." << std::endl;
        return 1;
    }

    // custom input styling function.
    auto style_file_content_input_option = [&cursor_index]() { // soon change to have focused customizations like underlined when focused.
        // input, main content
        ftxui::InputOption file_content_input_option_styling;

        file_content_input_option_styling.cursor_position = &cursor_index;
        file_content_input_option_styling.multiline = true; // explicitly enable multiline

        file_content_input_option_styling.transform = [](ftxui::InputState state) {
            auto e = state.element;
            if (state.focused){
                return e;
            }
            return e;
        };
        return file_content_input_option_styling;
    };

    // line increment in every line.
    ftxui::Component file_content_count_component = ftxui::Renderer([&content, &show_line_numbers, &cursor_index, &underline_active_row_on_numbers]() {
        uint32_t total_lines = std::count(content.begin(), content.end(), '\n');
        if (!content.empty()) {
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

    ftxui::Component file_content_input = ftxui::Input(&content, "", style_file_content_input_option());    

    std::string current_events = "System Ready. Select a menu above.";
    ftxui::Element current_events_links_Itahi = ftxui::hyperlink("<LINK>", ftxui::text("Itahi Text Editor " + ITAHI_VERSION));
    ftxui::Element current_events_links_FTXUI = ftxui::hyperlink("<LINK>", ftxui::text(" Built With FTXUI"));
    
    // styling

    // left panel file styling

    auto left_panel_file_styling = []() {
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();

        option.transform = [](const ftxui::EntryState& current_state) {
            auto e = ftxui::text(current_state.label);
            
            if (current_state.focused) {
                return ftxui::bold(e);
            }
            return e;
        };

        return option;
    };

    // numeric input custom
    auto make_numeric_input_row = [](std::string& target_var, std::string label_text, int max_digits_taken, int max_width) {
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
    };

    auto create_plain_button_option_symbol = []() {
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.transform = [](const ftxui::EntryState& state) {
            auto e = ftxui::bold(ftxui::text(" " + state.label + " "));
            if (state.focused) {
                return ftxui::bgcolor(ftxui::Color::White, ftxui::color(ftxui::Color::Black, e));
            }
            return e;
        };
        return option;
    };

    auto create_plain_button_option = []() {
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.transform = [](const ftxui::EntryState& state) {
            auto e = ftxui::text("< " + state.label + " >");
            if (state.focused) {
                return ftxui::bold(e);
            }
            return e;
        };
        return option;
    };

    auto create_plain_checkbox_option = []() {
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
    };

    // text button array
    auto create_text_button = [&](std::string label, int tab_index) {
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
    };

    auto btn_file = create_text_button("File", 0);
    auto btn_edit = create_text_button("Edit", 1);
    auto btn_view = create_text_button("View", 2);
    // auto btn_term = create_text_button("Terminal", 3);
    auto btn_settings = create_text_button("Settings", 3);
    auto btn_help = create_text_button("Help", 4);

    auto menu_bar_container = ftxui::Container::Horizontal({
        btn_file,
        btn_edit,
        btn_view,
        // btn_term,
        btn_settings,
        btn_help,
    });

    auto top_bar = ftxui::Renderer(menu_bar_container, [&] {
        return ftxui::hbox({
            ftxui::flex(
                menu_bar_container->Render()
            ),
            ftxui::flex(
                ftxui::center(
                    ftxui::bold(ftxui::text(active_file.empty() ? "--" : active_file))
                )
            ),
            ftxui::flex(
                ftxui::hbox({SEPARATOR_TRANSPARENT})
            )
        });
    });

    // functions
    // --- Back Menu ---
    auto file_back_button = ftxui::Button("<-", [&current_events, &status, &tab_selected] {
        current_events = "Action Executed: Back Button Pressed"; // change soon to go back to the text editor. Function.
        status = 1;
        tab_selected = 0;
    }, create_plain_button_option_symbol());

    auto file_back_button_container = ftxui::Container::Horizontal({
        file_back_button,
    });

    // --- File Menu Choices ---
    auto file_toggle_autosave = ftxui::Checkbox("Enable Auto-Save", &auto_save, create_plain_checkbox_option());
    auto file_btn_new_file = ftxui::Button("New File", [&] {
        current_events = "Action Executed: Created New File";
    }, create_plain_button_option());
    auto file_btn_save_file = ftxui::Button("Save File", [&] {
        current_events = "Action Executed: Saved Current File";
    }, create_plain_button_option());

    auto file_tab_container = ftxui::Container::Vertical({
        file_toggle_autosave,
        file_btn_new_file,
        file_btn_save_file,
    });

    // --- Edit Menu Choices ---
    // auto edit_toggle_word_wrap = ftxui::Checkbox("Enable Word Wrap", create_plain_checkbox_option());
    auto edit_btn_undo = ftxui::Button("Undo", [&] {
        current_events = "Action Executed: Undo";
    }, create_plain_button_option());
    auto edit_btn_redo = ftxui::Button("Redo", [&] {
        current_events = "Action Executed: Redo";
    }, create_plain_button_option());

    auto edit_tab_container = ftxui::Container::Vertical({
        //edit_toggle_word_wrap,
        edit_btn_undo,
        edit_btn_redo,
    });

    // --- View Menu Choices ---
    auto toggle_linenums = ftxui::Checkbox("Show Line Numbers", &show_line_numbers, create_plain_checkbox_option());
    auto toggle_underline_active_row_on_numbers = ftxui::Checkbox("Show Underline On Active Row On Numbers", &underline_active_row_on_numbers, create_plain_checkbox_option());
    // auto toggle_statusbar = ftxui::Checkbox("Show Status Bar", &show_status_bar, create_plain_checkbox_option());

    auto view_tab_container = ftxui::Container::Vertical({
        toggle_linenums,
        toggle_underline_active_row_on_numbers,
        //toggle_statusbar,
    });

    // --- Terminal Menu Choices ---
    // auto btn_clear_term = ftxui::Button("Clear Output Console", [&] {
    //     current_events = "Action Executed: Terminal Cleared";
    // }, create_plain_button_option());
    // auto btn_build_project = ftxui::Button("Run CMake Build", [&] {
    //     current_events = "Action Executed: CMake Build Started...";
    // }, create_plain_button_option());

    // auto term_tab_container = ftxui::Container::Vertical({
    //     btn_clear_term,
    //     btn_build_project,
    // });

    // --- Settings Menu Choices ---

    auto mouse_sens_row = make_numeric_input_row(mouse_sensitivity, "Mouse Sensitivity: ", MAX_DIGITS_LENGTH_DEFAULT, MAX_WIDTH_INPUT_INT_DEFAULT);

    auto settings_tab_container = ftxui::Container::Vertical({
        mouse_sens_row,
        // tab_size_row, // Just add more column 
    });


    // --- Help Menu Choices ---
    auto btn_about = ftxui::Button("About Itahi Editor", [&] {
        current_events = "Directing To Itahi Editor Repository.";
        open_url("https://github.com/Mclorl/Itahi-Terminal-Text-Editor");
    }, create_plain_button_option());

    auto btn_ftxui_about = ftxui::Button("About FTXUI", [&] {
        current_events = "Directing To FTXUI Repository.";
        open_url("https://github.com/ArthurSonzogni/FTXUI");
    }, create_plain_button_option());

    auto warning_text_link_direct = ftxui::Button("Once clicked it will direct you to a website!", [&]{
        current_events = "Once you click one of the buttons below it will direct you to the github repository.";
    }, create_plain_button_option());

    auto help_tab_container = ftxui::Container::Vertical({
        warning_text_link_direct,
        btn_about,
        btn_ftxui_about,
    });

    // --- Left Panel ---

    // left panel array.
    std::vector<ftxui::Component> left_panel_button_file_arr;
    int iterations = 0;

    // left panel file button function

    std::string target_dir = ".";
    if (argc > 1 && fs::is_directory(fs::status(argv[1]))) {
        target_dir = argv[1];
    } else if (argc > 1 && fs::is_regular_file(fs::status(argv[1]))) {
        content = readFile(argv[1]);
    }

    current_directory_content = iterate_current_path_shallow(target_dir);

    std::vector<std::string> file_list;
    std::stringstream s(current_directory_content);
    std::string line;
    while (std::getline(s, line)) {
        if (!line.empty()) {
            file_list.push_back(line);
        }
    }

    opened_directory_file_entry(left_panel_button_file_arr, left_panel_file_styling, file_list, content, active_file);

    auto left_panel_button_file_container = ftxui::Container::Vertical({
        std::move(left_panel_button_file_arr), 
        // std::move() casts an object into an rvalue so it does not effect the performance.
    });

    auto main_view = ftxui::Renderer(file_content_input, [&file_content_input, &file_content_count_component]{

        return 
            ftxui::flex(
                ftxui::vbox({
                    ftxui::flex(
                        ftxui::border(
                            ftxui::vbox(
                                ftxui::vscroll_indicator(
                                    ftxui::frame(
                                        ftxui::hbox({
                                            file_content_count_component->Render(),
                                            SEPARATOR_TRANSPARENT,
                                            file_content_input->Render()
                                        })
                                    )
                                )
                            )
                        )
                    )
                })
            );
    });

    // Master Tab Container
    auto tab_content = ftxui::Container::Tab({
        main_view,
        file_tab_container,
        edit_tab_container,
        view_tab_container,
        // term_tab_container,
        settings_tab_container,
        help_tab_container,
    }, &tab_selected);

    

    auto main_content_container = ftxui::Container::Vertical({
        file_back_button_container,
        tab_content,
    });

    // left panel or file explorer system.
    auto left_panel = ftxui::Renderer(left_panel_button_file_container, [&] {
        return ftxui::border(
            ftxui::vbox({
                ftxui::bold(
                    ftxui::text("Explorer")
                ),
                ftxui::separator(),
                left_panel_button_file_container->Render(),
                
            })
        );
    });

    auto main_content = ftxui::Renderer(main_content_container, [&] {

        if (status == 1) {
            return tab_content->Render();
        }
        if (status == 2) {
            return ftxui::border(
                ftxui::vbox({

                    ftxui::hbox({

                        file_back_button_container->Render(),
                        ftxui::separator(),
                        ftxui::bold(
                            ftxui::text("Menu Options:")
                        ),
                    }),
                    
                    ftxui::separator(),

                    ftxui::flex(
                        tab_content->Render()
                    ),
                    

                    // ftxui::separator(),

                    // ftxui::flex(
                    //     ftxui::vbox({
                    //         ftxui::bold(
                    //             ftxui::text("1 // Itahi Text Editor Workspace")
                    //         ),
                    //         ftxui::text("2 // Auto-Save: " + std::string(auto_save ? "ENABLED" : "DISABLED")),
                    //         ftxui::text("3 // Word Wrap: " + std::string(word_wrap ? "ENABLED" : "DISABLED")),
                    //         ftxui::text("4 // Line Numbers: " + std::string(show_line_numbers ? "VISIBLE" : "HIDDEN")),
                    //     })
                    // ),

                    ftxui::separator(),
                    ftxui::dim(
                        ftxui::text("Status: " + current_events)
                    )
                })
            );
        }
        return ftxui::border(
            ftxui::text("Error.")
        );
    });

    auto body_split = main_content;

    // if (argc > 1 && fs::is_directory(fs::status(argv[1]))) { // opened the program with a directory argument, open that directory.
    //     body_split = ftxui::ResizableSplitLeft(left_panel, main_content, &left_panel_width);
    //     current_directory_content = iterate_current_path_shallow(argv[1]);
    // } else if (argc == 1) { // if it has no arguments, open the current directory of the user.
    //     body_split = ftxui::ResizableSplitLeft(left_panel, main_content, &left_panel_width);
    //     // std::string temp_dir = get_current_path(ec).filename().string() + '/';
    //     current_directory_content = iterate_current_path_shallow(".");
    // } else { // if it has an argument file
    //     body_split = main_content;

    //     content = readFile(argv[1]);      
    // }

    if (argc > 1 && fs::is_regular_file(fs::status(argv[1]))) {
        // single file mode: hide the left explorer panel
        body_split = main_content;
    } else {
        // directory mode (or no args): show explorer panel on the left
        body_split = ftxui::ResizableSplitLeft(left_panel, main_content, &left_panel_width);
    }

    auto constrained_split = ftxui::CatchEvent(body_split, [&](ftxui::Event event) {
        // mouse scroll
        if (event.is_mouse()) {
            auto mouse = event.mouse();

            if (mouse.button == ftxui::Mouse::WheelUp) {
                for (int i = 0; i < std::stoi(mouse_sensitivity); i++) {
                    body_split->OnEvent(ftxui::Event::ArrowUp);
                }
                return true;
            }

            if (mouse.button == ftxui::Mouse::WheelDown) {
                for (int i = 0; i < std::stoi(mouse_sensitivity); i++) {
                    body_split->OnEvent(ftxui::Event::ArrowDown);
                }
                return true;
            }
        } 
        
        bool handled = body_split->OnEvent(event);

        const int MIN_LEFT_WIDTH = 25;
        const int MAX_LEFT_WIDTH = 100;

        if (left_panel_width < MIN_LEFT_WIDTH) {
            left_panel_width = MIN_LEFT_WIDTH;
        }
        if (left_panel_width > MAX_LEFT_WIDTH) {
            left_panel_width = MAX_LEFT_WIDTH;
        }

        return handled;
    });

    auto main_container = ftxui::Container::Vertical({
        top_bar,
        constrained_split,
    });

    auto document = ftxui::Renderer(main_container, [&] {
        return ftxui::vbox({
            ftxui::color(ftxui::Color::GrayDark,
                ftxui::window(
                    ftxui::bold(
                        ftxui::text("Itahi Text Editor " + ITAHI_VERSION)
                    ),

                    ftxui::color(ftxui::Color::White,
                        ftxui::flex(
                            ftxui::vbox({
                                top_bar->Render(),

                                ftxui::flex(
                                    constrained_split->Render()
                                )
                            })
                        )
                    )
                )
            )
        });
    });

    screen.Loop(document);

    return 0;
}

// helper for custom interactions for custom styling (eg. just hovering isn't supported in FTXUI so it needs to be manual)