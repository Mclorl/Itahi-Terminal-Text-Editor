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

#include "terminal_handling.hpp"

#include "syntax_highlight.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    ftxui::ScreenInteractive screen_instance = ftxui::ScreenInteractive::Fullscreen();

    screen = &screen_instance;

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
    ftxui::Component toggle_syntax_highlighting = toggle_syntax_highlighting_func();
    ftxui::Component syntax_color_settings = syntax_color_settings_func();

    ftxui::Component mouse_sens_row = mouse_sens_row_func();
    ftxui::Component tab_size_row = tab_size_row_func();

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
        toggle_syntax_highlighting,
        syntax_color_settings,
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
        tab_size_row,
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
                                ftxui::frame(
                                    ftxui::vscroll_indicator(
                                        ftxui::hbox({
                                            file_content_count_component(content, show_line_numbers, cursor_index, underline_active_row_on_numbers)->Render(),
                                            SEPARATOR_TRANSPARENT,
                                            input_component->Render() | syntax_highlight(content, long_active_file_name)
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
        content_read_first[p.string()] = readFile(argv[1]);
        content_user_updated[p.string()] = "";

        // active_file = p.filename().string();
        long_active_file_name = p.string();
        content_already_read[p.string()] = true;

        if (!content_already_read[p.string()]) {
            content_already_read[p.string()] = true;
            content = content_read_first[p.string()];
        } else if (!content_user_updated[p.string()].empty()) {
            content = content_user_updated[p.string()];
        } else {
            content = content_read_first[p.string()];
        }

        active_file_not_saved = p.filename().string() + "*";
        active_file_saved = p.filename().string();
        long_active_file_name = p.string();
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
    opened_directory_file_entry(left_panel_button_file_container, left_panel_button_file_arr, left_panel_file_styling, file_list, content, active_file, long_active_file_name, left_panel_width, 0, content_already_read);

    auto main_content_container = ftxui::Container::Vertical({
        file_back_button_container,
        tab_content,
    });

    // left panel or file explorer system.
    int previous_left_panel_width = left_panel_width;

    // free wheel-scroll offset of the explorer. 0.0 = top, 1.0 = bottom.
    // The wheel drives the *view* only; it must never move the selection.
    float explorer_scroll_y = 0.0f;

    // true once the user scrolled with the wheel; the view then stops
    // following the selection until the selection itself changes.
    bool explorer_free_scrolled = false;
    bool previous_panel_focused = false;
    std::string previous_selected_file_path;

    // screen region occupied by the explorer. CatchEvent intercepts events
    // regardless of the cursor position, so we reflect the explorer's box to
    // route mouse-wheel events to the correct pane.
    ftxui::Box left_panel_box;

    // full (potentially larger than the viewport) box of the explorer content.
    // Used, together with the reflected per-row boxes, to keep the view
    // aligned with the selected row.
    ftxui::Box left_panel_content_box;

    auto left_panel = ftxui::Renderer(left_panel_button_file_container, [&] {
        if (left_panel_width != previous_left_panel_width) { // if the previous panel width is no longer equal to the current panel width then update the display of the left panel.
            previous_left_panel_width = left_panel_width;

            left_panel_button_file_container->DetachAllChildren();
            left_panel_button_file_arr.clear();

            opened_directory_entry(screen, left_panel_button_file_container, left_panel_button_file_arr, left_panel_directory_styling, left_panel_file_styling, file_list, opened_folders, content, active_file, long_active_file_name, left_panel_button_file_container, left_panel_width, 0);
            opened_directory_file_entry(left_panel_button_file_container, left_panel_button_file_arr, left_panel_file_styling, file_list, content, active_file, long_active_file_name, left_panel_width, 0, content_already_read);
        }

        ftxui::Element entries = left_panel_button_file_container->Render() | capture_box(left_panel_content_box);

        const bool panel_focused = left_panel_button_file_container->Focused();

        if (panel_focused && !previous_panel_focused) {
            // just gained focus: follow the selection again.
            explorer_free_scrolled = false;
            previous_selected_file_path = left_panel_selected_file_path;
        }
        if (panel_focused && left_panel_selected_file_path != previous_selected_file_path) {
            // the selection changed (keyboard or click): follow it again.
            previous_selected_file_path = left_panel_selected_file_path;
            explorer_free_scrolled = false;
        }
        previous_panel_focused = panel_focused;

        // while the frame follows the selection, keep the free-scroll offset in
        // sync with the selected row so that starting to wheel (or losing
        // focus) does not jump the view.
        if (panel_focused && !explorer_free_scrolled) {
            const auto it = left_panel_row_boxes.find(left_panel_selected_file_path);
            const int span = left_panel_content_box.y_max - left_panel_content_box.y_min;
            if (it != left_panel_row_boxes.end() && span > 0) {
                const int row = it->second.y_min - left_panel_content_box.y_min;
                explorer_scroll_y = std::clamp(
                    static_cast<float>(row) / static_cast<float>(span), 0.0f, 1.0f);
            }
        }

        // native focus following is used while the selection drives the view;
        // otherwise the view is driven by `explorer_scroll_y`.
        if (!panel_focused || explorer_free_scrolled) {
            entries = entries | ftxui::focusPositionRelative(0.0f, explorer_scroll_y);
        }

        return ftxui::border(
            ftxui::vbox({
                ftxui::bold(
                    ftxui::text("Explorer")
                ),
                ftxui::separator(),
                ftxui::frame(
                    ftxui::vscroll_indicator(
                        entries
                    )
                ),
            })
        ) | ftxui::reflect(left_panel_box) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, left_panel_width);
    });

    auto main_content = ftxui::Renderer(main_content_container, [&] {

        if (status == 1) {
            // the "<-" button isn't drawn in this view: once it closed the menu
            // make the editor the active child, so keys go there instead of to
            // a hidden button. (Local to this container: never steals the focus
            // from the explorer or the top bar.)
            if (main_content_container->ActiveChild() == file_back_button_container) {
                main_content_container->SetActiveChild(tab_content.get());
            }
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

        if (mouse.button != ftxui::Mouse::WheelUp && mouse.button != ftxui::Mouse::WheelDown) {
            return false;
        }

        const bool wheel_up = mouse.button == ftxui::Mouse::WheelUp;

        // only react when the cursor is actually over the explorer, otherwise
        // let the wheel fall through to the editor.
        if (!left_panel_box.Contain(mouse.x, mouse.y)) {
            return false;
        }

        // scroll the view freely. This must never move the selection/focus, so
        // the wheel no longer forwards arrow keys to the container.
        explorer_free_scrolled = true;
        const float step = 0.03f * static_cast<float>(get_mouse_sensitivity());
        explorer_scroll_y += wheel_up ? -step : step;
        explorer_scroll_y = std::max(0.0f, std::min(1.0f, explorer_scroll_y));
        screen->PostEvent(ftxui::Event::Custom);

        return true;
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
        // custom separator: a vertical line with a "⋮" grip in the middle (like vscode)
        ftxui::ResizableSplitOption split_option;
        split_option.main = scrollable_left_panel;
        split_option.back = main_content;
        split_option.direction = ftxui::Direction::Left;
        split_option.main_size = &left_panel_width;
        split_option.separator_func = [] {
            return ftxui::vbox({
                ftxui::flex(SEPARATOR_TRANSPARENT),
                ftxui::bold(ftxui::text("⋮")),
                ftxui::flex(SEPARATOR_TRANSPARENT),
            });
        };
        body_split = ftxui::ResizableSplit(split_option);
    }

    auto constrained_split = ftxui::CatchEvent(body_split, [&](ftxui::Event event) {
        // mouse scroll
        if (event.is_mouse()) {
            auto mouse = event.mouse();

            // If the cursor is over the explorer, let the event fall through to
            // `body_split` so the inner `scrollable_left_panel` can handle it.
            const bool over_explorer = left_panel_box.Contain(mouse.x, mouse.y);

            if (!over_explorer && mouse.button == ftxui::Mouse::WheelUp) {
                for (int i = 0; i < get_mouse_sensitivity(); i++) {
                    main_content->OnEvent(ftxui::Event::ArrowUp);
                }
                return true;
            }

            if (!over_explorer && mouse.button == ftxui::Mouse::WheelDown) {
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

    // normal mode panel navigation: Ctrl+Arrow or Ctrl+H/J/K/L move the focus
    // between the top bar, the explorer and the main content. Plain arrows
    // keep moving inside the focused panel.
    const bool has_explorer = body_split != main_content;
    bool came_from_explorer = false;

    auto panel_navigation = ftxui::CatchEvent(main_container, [&](ftxui::Event event) {
        if (!is_normal_mode) {
            return false;
        }

        // Ctrl+H and Ctrl+J only arrive as their own events thanks to
        // patches/ftxui-ctrl-h-ctrl-j.patch (stock FTXUI merges them with
        // Backspace and Enter). Ctrl+J comes in as a CSI-u style sequence.
        const ftxui::Event ctrl_j = ftxui::Event::Special("\x1B[106;5u");

        const bool go_left  = event == ftxui::Event::ArrowLeftCtrl  || event == ftxui::Event::CtrlH;
        const bool go_right = event == ftxui::Event::ArrowRightCtrl || event == ftxui::Event::CtrlL;
        const bool go_up    = event == ftxui::Event::ArrowUpCtrl    || event == ftxui::Event::CtrlK;
        const bool go_down  = event == ftxui::Event::ArrowDownCtrl  || event == ctrl_j;

        if (!go_left && !go_right && !go_up && !go_down) {
            return false;
        }

        const bool in_top_bar  = top_bar_component->Focused();
        const bool in_explorer = has_explorer && left_panel_button_file_container->Focused();

        auto focus_explorer = [&] {
            if (has_explorer && left_panel_button_file_container->Focusable()) {
                left_panel_button_file_container->TakeFocus();
            }
        };

        if (go_up && !in_top_bar) {
            came_from_explorer = in_explorer;
            top_bar_component->TakeFocus();
        } else if (go_down && in_top_bar) {
            if (came_from_explorer) {
                focus_explorer();
            } else {
                tab_content->TakeFocus();
            }
        } else if (go_left && !in_top_bar && !in_explorer) {
            focus_explorer();
        } else if (go_right && in_explorer) {
            tab_content->TakeFocus();
        }

        // always consumed in normal mode, even when there is nowhere to go,
        // so the shortcut never falls through to the focused component.
        return true;
    });

    auto document = ftxui::Renderer(panel_navigation, [&] {
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

    ftxui::Component complete_document = key_listener_function(*screen, document);

    screen->Loop(complete_document);

    return 0;
}

// helper for custom interactions for custom styling (eg. just hovering isn't supported in FTXUI so it needs to be manual)