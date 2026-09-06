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

// custom styling
#include "custom_styling.hpp"

// tab bar content
#include "tab_contents.hpp"

// global variables include
#include "global_variables.hpp"

#include <unordered_set>

#include "sets_of_tools.hpp"

#include "functions.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    auto screen = ftxui::ScreenInteractive::Fullscreen();

    int left_panel_width = 25; // Default width of the left panel slider

    // error catch var

    std::error_code ec;


    std::string current_directory_content;


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

    // change start here.

    // variables containers.
    ftxui::Component file_back_button = file_back_button_func();

    ftxui::Component file_toggle_autosave = file_toggle_autosave_func();
    ftxui::Component file_btn_new_file = file_btn_new_file_func();
    ftxui::Component file_btn_save_file = file_btn_save_file_func();

    ftxui::Component edit_btn_undo = edit_btn_undo_func();
    ftxui::Component edit_btn_redo = edit_btn_redo_func();

    ftxui::Component toggle_linenums = toggle_linenums_func();
    ftxui::Component toggle_underline_active_row_on_numbers = toggle_underline_active_row_on_numbers_func();

    ftxui::Component mouse_sens_row = mouse_sens_row_func();

    ftxui::Component warning_text_link_direct = warning_text_link_direct_func();
    ftxui::Component btn_about = btn_about_func();
    ftxui::Component btn_ftxui_about = btn_ftxui_about_func();

    // containers
    // tab containers
    // --- File Back Option ---
    ftxui::Component file_back_button_container = ftxui::Container::Horizontal({
        file_back_button,
    });

    // --- File Menu Choices ---
    ftxui::Component file_tab_container = ftxui::Container::Vertical({
        file_toggle_autosave,
        file_btn_new_file,
        file_btn_save_file,
    });

    // --- Edit Menu Choices ---

    ftxui::Component edit_tab_container = ftxui::Container::Vertical({
        //edit_toggle_word_wrap,
        edit_btn_undo,
        edit_btn_redo,
    });

    // --- View Menu Choices ---
    ftxui::Component view_tab_container = ftxui::Container::Vertical({
        toggle_linenums,
        toggle_underline_active_row_on_numbers,
        //toggle_statusbar,
    });

    // --- Terminal Menu Choices ---
    // ftxui::Component term_tab_container = ftxui::Container::Vertical({
    //     btn_clear_term,
    //     btn_build_project,
    // });

    // --- Settings Menu Choices ---
    ftxui::Component settings_tab_container = ftxui::Container::Vertical({
        mouse_sens_row,
        // tab_size_row, // Just add more column 
    });

    // --- Help Menu Choices ---

    ftxui::Component help_tab_container = ftxui::Container::Vertical({
        warning_text_link_direct,
        btn_about,
        btn_ftxui_about,
    });

    // main view container
    ftxui::Component input_component = file_content_input(content, cursor_index);

    ftxui::Component main_view = ftxui::Renderer(input_component, [input_component]{
        return 
            ftxui::flex(
                ftxui::vbox({
                    ftxui::flex(
                        ftxui::border(
                            ftxui::vbox(
                                ftxui::vscroll_indicator(
                                    ftxui::frame(
                                        ftxui::hbox({
                                            file_content_count_component(content, show_line_numbers, cursor_index, underline_active_row_on_numbers)->Render(),
                                            SEPARATOR_TRANSPARENT,
                                            input_component->Render()
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
    ftxui::Component tab_content = ftxui::Container::Tab({
        main_view,
        file_tab_container,
        edit_tab_container,
        view_tab_container,
        // term_tab_container,
        settings_tab_container,
        help_tab_container,
    }, &tab_selected);
    
    ftxui::Element current_events_links_Itahi = ftxui::hyperlink("<LINK>", ftxui::text("Itahi Text Editor " + ITAHI_VERSION));
    ftxui::Element current_events_links_FTXUI = ftxui::hyperlink("<LINK>", ftxui::text(" Built With FTXUI"));
    // --- Left Panel ---

    // left panel array.
    std::vector<ftxui::Component> left_panel_button_file_arr;
    auto left_panel_button_file_container = ftxui::Container::Vertical({});
    int iterations = 0;

    // left panel file button function

    std::string target_dir = ".";
    if (argc > 1 && fs::is_directory(fs::status(argv[1]))) {
        target_dir = argv[1];
    } else if (argc > 1 && fs::is_regular_file(fs::status(argv[1]))) {
        fs::path p(argv[1]);
        active_file = p.filename().string();
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

    opened_directory_entry(screen, left_panel_button_file_container, left_panel_button_file_arr, left_panel_directory_styling, left_panel_file_styling, file_list, opened_folders, content, active_file, long_active_file_name, left_panel_button_file_container, left_panel_width, 0);
    opened_directory_file_entry(left_panel_button_file_container, left_panel_button_file_arr, left_panel_file_styling, file_list, content, active_file, long_active_file_name, left_panel_width, 0);

    auto main_content_container = ftxui::Container::Vertical({
        file_back_button_container,
        tab_content,
    });

    // left panel or file explorer system.
    int previous_left_panel_width = left_panel_width;

    auto left_panel = ftxui::Renderer(left_panel_button_file_container, [&] {
        if (left_panel_width != previous_left_panel_width) { // if the previous panel width is no longer equal to the current panel width then update the display of the left panel.
            previous_left_panel_width = left_panel_width;

            left_panel_button_file_container->DetachAllChildren();
            left_panel_button_file_arr.clear();

            opened_directory_entry(screen, left_panel_button_file_container, left_panel_button_file_arr, left_panel_directory_styling, left_panel_file_styling, file_list, opened_folders, content, active_file, long_active_file_name, left_panel_button_file_container, left_panel_width, 0);
            opened_directory_file_entry(left_panel_button_file_container, left_panel_button_file_arr, left_panel_file_styling, file_list, content, active_file, long_active_file_name, left_panel_width, 0);
        }

        return ftxui::border(
            ftxui::vbox({
                ftxui::bold(
                    ftxui::text("Explorer")
                ),
                ftxui::separator(),
                ftxui::vscroll_indicator(
                    ftxui::frame(
                        left_panel_button_file_container->Render() 
                    )
                ),
            })
        ) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, left_panel_width);
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

    auto scrollable_left_panel = ftxui::CatchEvent(left_panel, [&](ftxui::Event event) {
        if (!event.is_mouse()) {
            return false;
        }

        const auto mouse = event.mouse();

        if (mouse.button == ftxui::Mouse::WheelUp) {
            for (int i = 0; i < get_mouse_sensitivity(); i++) {
                left_panel_button_file_container->OnEvent(ftxui::Event::ArrowUp);
            }

            return true;
        }

        if (mouse.button == ftxui::Mouse::WheelDown) {
            for (int i = 0; i < get_mouse_sensitivity(); i++) {
                left_panel_button_file_container->OnEvent(ftxui::Event::ArrowDown);
            }

            return true;
        }

        return false;
    });

    // default.
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
        body_split = ftxui::ResizableSplitLeft(scrollable_left_panel, main_content, &left_panel_width);
    }

    auto constrained_split = ftxui::CatchEvent(body_split, [&](ftxui::Event event) {
        // mouse scroll
        if (event.is_mouse()) {
            auto mouse = event.mouse();

            if (mouse.button == ftxui::Mouse::WheelUp) {
                for (int i = 0; i < get_mouse_sensitivity(); i++) {
                    main_content->OnEvent(ftxui::Event::ArrowUp);
                }
                return true;
            }

            if (mouse.button == ftxui::Mouse::WheelDown) {
                for (int i = 0; i < get_mouse_sensitivity(); i++) {
                    main_content->OnEvent(ftxui::Event::ArrowDown);
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
        top_bar_component,
        constrained_split,
    });

    auto document = ftxui::Renderer(main_container, [&] {
        return ftxui::vbox({
            ftxui::color(ftxui::Color::GrayDark,
                ftxui::color(ftxui::Color::White,
                    ftxui::flex(
                        ftxui::vbox({
                            top_bar_component->Render(),

                            ftxui::flex(
                                constrained_split->Render()
                            )
                        })
                    )
                )
            )
        });
    });

    screen.Loop(document);

    return 0;
}

// helper for custom interactions for custom styling (eg. just hovering isn't supported in FTXUI so it needs to be manual)