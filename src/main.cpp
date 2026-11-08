#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "core/file_io.hpp"
#include "core/version.hpp"
#include "editor/commands.hpp"
#include "editor/mode.hpp"
#include "editor/paths.hpp"
#include "editor/search.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/undo_history.hpp"
#include "editor/workspace.hpp"
#include "ui/command_line.hpp"
#include "ui/editor_view.hpp"
#include "ui/explorer.hpp"
#include "ui/key_listener.hpp"
#include "ui/menu_bar.hpp"
#include "ui/menu_pages.hpp"
#include "ui/search_paint.hpp"
#include "ui/syntax_paint.hpp"
#include "ui/ui_state.hpp"
#include "ui/widgets.hpp"
#include "ui/wrapped_view.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: itahi [file or folder]" << std::endl;
        return 1;
    }

    // one status check, without throwing. A path that doesn't exist yet is a
    // new file, created by the first ">w".
    std::error_code ec;
    const fs::file_status arg_status = argc > 1 ? fs::status(argv[1], ec) : fs::file_status{};
    const bool arg_is_new_file = argc > 1 && arg_status.type() == fs::file_type::not_found;
    const bool arg_is_file = argc > 1 && (fs::is_regular_file(arg_status) || arg_is_new_file);
    const bool arg_is_dir = argc > 1 && fs::is_directory(arg_status);
    if (argc > 1 && !arg_is_file && !arg_is_dir) {
        std::cerr << "Cannot open " << argv[1] << (ec ? ": " + ec.message() : std::string()) << std::endl;
        return 1;
    }

    ftxui::ScreenInteractive screen_instance = ftxui::ScreenInteractive::Fullscreen();
    screen = &screen_instance;

    int left_panel_width = 25; // Default width of the left panel slider

    // --- File Back Option ---
    ftxui::Component file_back_button_container = ftxui::Container::Horizontal({
        file_back_button_func(),
    });

    // --- File Menu Choices ---
    ftxui::Component file_tab_container = ftxui::Container::Vertical({
        file_btn_new_file_func(),
        file_btn_save_file_func(),
    });

    // --- Edit Menu Choices ---
    ftxui::Component edit_tab_container = ftxui::Container::Vertical({
        edit_btn_undo_func(),
        edit_btn_redo_func(),
    });

    // --- View Menu Choices ---
    ftxui::Component view_tab_container = ftxui::Container::Vertical({
        toggle_linenums_func(),
        toggle_underline_active_row_on_numbers_func(),
        toggle_underline_active_row_func(),
        toggle_syntax_highlighting_func(),
        toggle_word_wrap_func(),
        syntax_color_settings_func(),
    });

    // --- Settings Menu Choices ---
    ftxui::Component settings_tab_container = ftxui::Container::Vertical({
        mouse_sens_row_func(),
        tab_size_row_func(),
    });

    // --- Help Menu Choices ---
    ftxui::Component help_tab_container = ftxui::Container::Vertical({
        warning_text_link_direct_func(),
        btn_about_func(),
        btn_ftxui_about_func(),
    });

    // created further down, once the containers it gives the focus back to exist.
    ftxui::Component command_line_component;

    // vim-like bottom line, drawn inside the border of the main content: the
    // command line while typing a command (with the error of a rejected
    // command above it). Otherwise the last error (red) or result (dim)
    // until the next key press.
    auto command_line_element = [&] {
        auto error_line = [] {
            return ftxui::color(ftxui::Color::White,
                ftxui::bgcolor(ftxui::Color::Red,
                    ftxui::text(command_error)
                )
            );
        };

        if (mode == Mode::Command) {
            ftxui::Element line = command_line_component->Render();
            // a search shows where it is: "2 of 5", like vscode.
            const SearchState& search = search_state();
            if (search.active && !search.pattern.empty()) {
                const std::string count = search.matches.empty() ? "No results"
                    : search.current < 0 ? std::to_string(search.matches.size()) + (search.matches.size() == 1 ? " match" : " matches")
                    : std::to_string(search.current + 1) + " of " + std::to_string(search.matches.size());
                line = ftxui::hbox({line | ftxui::flex, ftxui::dim(ftxui::text(" " + count + "  Enter/Down: next  Up: previous  Esc: close "))});
            }
            // Tab's options: on a gray strip under the command line, every
            // name, wrapped across as many rows as needed.
            ftxui::Element options = ftxui::emptyElement();
            if (!command_completions.empty()) {
                ftxui::Elements names;
                for (const std::string& name : command_completions) {
                    names.push_back(ftxui::text(name + "   "));
                }
                options = ftxui::color(ftxui::Color::White,
                    ftxui::bgcolor(ftxui::Color::GrayDark,
                        ftxui::flexbox(std::move(names)) | ftxui::xflex));
            }
            if (command_error.empty()) {
                return ftxui::vbox({line, options});
            }
            return ftxui::vbox({
                error_line(),
                line,
                options,
            });
        }
        if (!command_error.empty()) {
            return error_line();
        }
        if (!command_output.empty()) {
            return ftxui::dim(ftxui::text(command_output));
        }
        return ftxui::emptyElement();
    };

    // main view container
    ftxui::Component input_component = file_content_input(content, cursor_index);

    // "Open Anyway" for a binary / unsupported encoding file (like vscode).
    ftxui::Component open_anyway_button = ftxui::Button("Open Anyway", [] {
        files_opened_anyway.insert(long_active_file_name);
    }, create_plain_button_option());

    // what the main view shows, chosen on every render:
    // the editor, the binary file warning, or the greeting when no file is open.
    enum MainViewPage { EDITOR_PAGE, BINARY_WARNING_PAGE, GREETING_PAGE };
    int main_view_page = EDITOR_PAGE;

    ftxui::Component main_view_pages = ftxui::Container::Tab({
        input_component,
        open_anyway_button,
        ftxui::Renderer([] { return ftxui::emptyElement(); }), // greeting: nothing to focus
    }, &main_view_page);

    // the line holding the cursor, counted from 0.
    auto cursor_line = [] {
        const std::size_t at = std::min(static_cast<std::size_t>(std::max(cursor_index, 0)), content.size());
        return static_cast<int>(std::count(content.begin(), content.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
    };

    // the editor's visible area, measured each frame: word wrap needs its width.
    ftxui::Box editor_box;
    int last_wrap_width = 0;

    ftxui::Component main_view = ftxui::Renderer(main_view_pages, [&]{
        main_view_page = long_active_file_name.empty() ? GREETING_PAGE
                       : is_file_hidden_from_editor(long_active_file_name) ? BINARY_WARNING_PAGE
                       : EDITOR_PAGE;

        ftxui::Element page;
        if (main_view_page == EDITOR_PAGE && word_wrap) {
            // text width = the area minus the gutter, its separator and the scroll bar.
            const int gutter = wrapped_gutter_width(content, show_line_numbers);
            const int area = editor_box.x_max - editor_box.x_min + 1;
            int width = area - (gutter > 0 ? gutter + 1 : 0) - 1;
            if (area <= 1) {
                width = screen->dimx() / 2;  // first frame: not measured yet
            }
            if (width != last_wrap_width) {
                last_wrap_width = width;
                screen->PostEvent(ftxui::Event::Custom);  // re-wrap with the measured width
            }
            page = ftxui::frame(
                white_vscroll_indicator(
                    wrapped_editor_view(content, long_active_file_name, cursor_index, width,
                                        show_line_numbers, underline_active_row_on_numbers, underline_active_row)
                )
            ) | ftxui::reflect(editor_box);
        } else if (main_view_page == EDITOR_PAGE) {
            page = ftxui::frame(
                white_vscroll_indicator(
                    ftxui::hbox({
                        file_content_count_component(content, show_line_numbers, cursor_index, underline_active_row_on_numbers)->Render(),
                        ftxui::separatorEmpty(),
                        input_component->Render() | syntax_highlight(content, long_active_file_name)
                            | search_highlight(content)
                            | underline_row(underline_active_row ? cursor_line() : -1)
                    })
                )
            ) | ftxui::reflect(editor_box);
        } else if (main_view_page == BINARY_WARNING_PAGE) {
            page = ftxui::vcenter(
                ftxui::vbox({
                    ftxui::hcenter(
                        ftxui::bold(
                            ftxui::color(ftxui::Color::Yellow, ftxui::text("Warning"))
                        )
                    ),
                    ftxui::separatorEmpty(),
                    ftxui::paragraphAlignCenter("The file is not displayed in the text editor because it is either binary or uses an unsupported text encoding."),
                    ftxui::dim(
                        ftxui::paragraphAlignCenter("Opening it anyway could break the Itahi Text Editor.")
                    ),
                    ftxui::separatorEmpty(),
                    ftxui::hcenter(
                        open_anyway_button->Render()
                    ),
                })
            );
        } else {
            page = ftxui::vcenter(
                ftxui::vbox({
                    ftxui::hcenter(
                        ftxui::bold(ftxui::text("Welcome to Itahi Text Editor" + ITAHI_VERSION))
                    ),
                    ftxui::separatorEmpty(),
                    ftxui::dim(
                        ftxui::paragraphAlignCenter("Select a file in the Explorer panel on the left to start editing.")
                    ),
                })
            );
        }

        return ftxui::flex(
            ftxui::border(
                ftxui::vbox({
                    ftxui::flex(page),
                    command_line_element(),
                })
            )
        );
    });

    // Master Tab Container
    ftxui::Component tab_content = ftxui::Container::Tab({
        main_view,
        file_tab_container,
        edit_tab_container,
        view_tab_container,
        settings_tab_container,
        help_tab_container,
    }, &tab_selected);

    // --- Left Panel ---
    auto left_panel_button_file_container = ftxui::Container::Vertical({});

    fs::path target_dir = ".";
    if (arg_is_dir) {
        target_dir = argv[1];
    } else if (arg_is_file) {
        // single file mode: open it now (a missing file becomes a new buffer).
        if (!open_file(argv[1], content, active_file, long_active_file_name)) {
            std::cerr << "Cannot open " << command_error << std::endl;
            return 1;
        }
    }

    workspace_root = arg_is_file ? fs::path(argv[1]).parent_path().string() : target_dir.string();
    if (workspace_root.empty()) {
        workspace_root = ".";
    }

    std::vector<fs::path> file_list;
    if (!arg_is_file) {
        std::string list_error;
        file_list = list_directory(target_dir, list_error);
        if (!list_error.empty()) {
            std::cerr << "Cannot open " << list_error << std::endl;
            return 1;
        }
    }

    auto fill_explorer = [&] {
        left_panel_button_file_container->DetachAllChildren();
        opened_directory_entry(screen, file_list, opened_folders, content, active_file, long_active_file_name,
                               left_panel_button_file_container, left_panel_width, 0);
        opened_directory_file_entry(left_panel_button_file_container, file_list, content, active_file,
                                    long_active_file_name, left_panel_width, 0);
    };
    fill_explorer();

    // a file was created (>e, New File, >w name): list the folders again.
    request_explorer_refresh = [&] {
        if (arg_is_file) {
            return;  // single file mode: no explorer
        }
        std::string list_error;
        file_list = list_directory(target_dir, list_error);
        fill_explorer();
        screen->PostEvent(ftxui::Event::Custom);
    };

    // component that had the focus before command mode, given it back when
    // the command line closes.
    ftxui::Component focus_before_command_mode = tab_content;

    ftxui::Component main_content_container; // used by the command line below

    command_line_component = ftxui::Maybe(
        command_line_input([&] {
            set_normal_mode();
            command_line.clear();
            command_error.clear();
            // the hidden command line must not stay the active child.
            main_content_container->SetActiveChild(tab_content.get());
            focus_before_command_mode->TakeFocus();
        }),
        [] { return mode == Mode::Command; }
    );

    main_content_container = ftxui::Container::Vertical({
        file_back_button_container,
        tab_content,
        command_line_component,
    });

    // left panel or file explorer system.
    int previous_left_panel_width = left_panel_width;

    // the explorer's viewport: the wheel moves it by whole rows; moving the
    // selection scrolls it only as far as needed to keep the row visible.
    ScrollState explorer_scroll;
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
        if (left_panel_width != previous_left_panel_width) {
            // names are cut to the panel width when the rows are built, so a
            // new width needs new rows.
            previous_left_panel_width = left_panel_width;
            fill_explorer();
        }

        ftxui::Element entries = left_panel_button_file_container->Render() | capture_box(left_panel_content_box);

        const bool panel_focused = left_panel_button_file_container->Focused();

        // the selection changed (keyboard or click), or the explorer just got
        // the focus: scroll just enough to show the selected row. Row boxes
        // are from the last frame, which is fine: rows don't move on their own.
        const bool follow = panel_focused &&
            (!previous_panel_focused || left_panel_selected_file_path != previous_selected_file_path);
        previous_panel_focused = panel_focused;
        previous_selected_file_path = left_panel_selected_file_path;
        if (follow) {
            const auto it = left_panel_row_boxes.find(left_panel_selected_file_path);
            if (it != left_panel_row_boxes.end()) {
                explorer_scroll.keep_visible(it->second.y_min - left_panel_content_box.y_min);
            }
        }

        return ftxui::border(
            ftxui::vbox({
                ftxui::bold(
                    ftxui::text("Explorer")
                ),
                ftxui::separator(),
                scroll_frame(
                    white_vscroll_indicator(
                        entries
                    ),
                    explorer_scroll
                ) | ftxui::flex,
            })
        ) | ftxui::reflect(left_panel_box) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, left_panel_width);
    });

    auto main_view_or_menu = [&] {
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
                command_line_element(),
            })
        );
    };

    auto main_content = ftxui::Renderer(main_content_container, [&] {
        return main_view_or_menu();
    });

    auto scrollable_left_panel = ftxui::CatchEvent(left_panel, [&](ftxui::Event event) {
        if (!event.is_mouse()) {
            return false;
        }

        const auto mouse = event.mouse();

        if (mouse.button != ftxui::Mouse::WheelUp && mouse.button != ftxui::Mouse::WheelDown) {
            return false;
        }

        // only react when the cursor is actually over the explorer, otherwise
        // let the wheel fall through to the editor.
        if (!left_panel_box.Contain(mouse.x, mouse.y)) {
            return false;
        }

        // scroll the view by whole rows. This must never move the selection
        // or the focus.
        const int rows = get_mouse_sensitivity();
        explorer_scroll.scroll_by(mouse.button == ftxui::Mouse::WheelUp ? -rows : rows);
        screen->PostEvent(ftxui::Event::Custom);

        return true;
    });

    ftxui::Component body_split = main_content;
    if (!arg_is_file) {
        // directory mode (or no args): show explorer panel on the left
        // custom separator: a vertical line with a "⋮" grip in the middle (like vscode)
        ftxui::ResizableSplitOption split_option;
        split_option.main = scrollable_left_panel;
        split_option.back = main_content;
        split_option.direction = ftxui::Direction::Left;
        split_option.main_size = &left_panel_width;
        split_option.separator_func = [] {
            return ftxui::vbox({
                ftxui::flex(ftxui::separatorEmpty()),
                ftxui::bold(ftxui::text("⋮")),
                ftxui::flex(ftxui::separatorEmpty()),
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

            const bool wheel = mouse.button == ftxui::Mouse::WheelUp || mouse.button == ftxui::Mouse::WheelDown;

            if (!over_explorer && wheel) {
                const ftxui::Event arrow = mouse.button == ftxui::Mouse::WheelUp ? ftxui::Event::ArrowUp : ftxui::Event::ArrowDown;

                // the editor gets the arrows directly: FTXUI containers ignore
                // keys while they are not focused, so going through
                // main_content did nothing while the explorer, the top bar or
                // the command line had the focus.
                ftxui::Component target = status == 1 ? input_component : main_content;

                // nothing to scroll while the editor is hidden (warning / greeting).
                if (status == 1 && main_view_page != EDITOR_PAGE) {
                    return true;
                }

                for (int i = 0; i < get_mouse_sensitivity(); i++) {
                    target->OnEvent(arrow);
                }
                return true;
            }
        }

        bool handled = body_split->OnEvent(event);

        const int MIN_LEFT_WIDTH = 25;
        const int MAX_LEFT_WIDTH = 100;
        left_panel_width = std::clamp(left_panel_width, MIN_LEFT_WIDTH, MAX_LEFT_WIDTH);

        return handled;
    });

    ftxui::Component top_bar_component = make_top_bar(active_file);

    auto main_container = ftxui::Container::Vertical({
        top_bar_component,
        constrained_split,
    });

    // hooks for the editor layer, which doesn't know about FTXUI.
    request_redraw = [] { screen->PostEvent(ftxui::Event::Custom); };
    request_quit = [] { screen->ExitLoopClosure()(); };

    // opening a file (or a search) closes any menu and gives the editor the focus.
    show_editor = [&] {
        status = 1;
        tab_selected = 0;
        tab_content->TakeFocus();
    };

    // opens the command line with `text` typed after the '>' prompt.
    auto start_command_line = [&](const std::string& text) {
        if (top_bar_component->Focused()) {
            focus_before_command_mode = top_bar_component;
        } else if (!arg_is_file && left_panel_button_file_container->Focused()) {
            focus_before_command_mode = left_panel_button_file_container;
        } else if (file_back_button_container->Focused()) {
            focus_before_command_mode = file_back_button_container;
        } else {
            focus_before_command_mode = tab_content;
        }

        command_line = ">" + text;
        command_cursor_index = static_cast<int>(command_line.size());
        command_error.clear();
        set_command_mode();
        command_line_component->TakeFocus();
    };
    open_command_line = start_command_line;

    // normal mode panel navigation: Ctrl+Arrow or Ctrl+H/J/K/L move the focus
    // between the top bar, the explorer and the main content. Plain arrows
    // keep moving inside the focused panel.
    const bool has_explorer = !arg_is_file;
    bool came_from_explorer = false;
    bool in_paste = false;

    auto show_result = [](const CommandResult& result) {
        (result.ok ? command_output : command_error) = result.message;
    };

    auto panel_navigation = ftxui::CatchEvent(main_container, [&](ftxui::Event event) {
        // bracketed paste: pasted text is data, never a command.
        if (event == ftxui::Event::Special("\x1B[200~")) { in_paste = true; return true; }
        if (event == ftxui::Event::Special("\x1B[201~")) { in_paste = false; return true; }
        if (in_paste && mode != Mode::Edit) { return true; }

        // Esc always leaves Edit mode, wherever the focus is.
        if (mode == Mode::Edit && event == ftxui::Event::Escape) {
            set_normal_mode();
            return true;
        }

        // familiar shortcuts, in Normal and Edit mode.
        if (mode != Mode::Command) {
            if (event == ftxui::Event::CtrlS) { show_result(save_active_file()); return true; }
            if (event == ftxui::Event::CtrlZ) { show_result(undo_edit()); return true; }
            if (event == ftxui::Event::CtrlY) { show_result(redo_edit()); return true; }
        }

        // Edit mode, but the focus is outside the text: keys that would be
        // commands or shortcuts in Normal mode do nothing here, so say why.
        // (Arrows, Enter and Tab still move around menus and the explorer.)
        if (mode == Mode::Edit && !input_component->Focused()) {
            const bool command_key = event.is_character() ||
                event == ftxui::Event::ArrowLeftCtrl || event == ftxui::Event::ArrowRightCtrl ||
                event == ftxui::Event::ArrowUpCtrl || event == ftxui::Event::ArrowDownCtrl ||
                event == ftxui::Event::CtrlH || event == ftxui::Event::CtrlK || event == ftxui::Event::CtrlL ||
                event == ftxui::Event::Special("\x1B[106;5u");
            if (command_key) {
                command_error = "You are in Edit mode: commands and key shortcuts only work in Normal mode. Press Esc to switch.";
                return true;
            }
        }

        if (mode != Mode::Normal) {
            return false;
        }

        // '>' opens the command line from anywhere (editor, explorer, menus).
        if (event == ftxui::Event::Character('>')) {
            start_command_line("");
            return true;
        }
        // '/' and '?' search down / up in the open file (like vim).
        if ((event == ftxui::Event::Character('/') || event == ftxui::Event::Character('?')) && !long_active_file_name.empty()) {
            start_command_line(event.character());
            return true;
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

    // FTXUI draws the first frame from wherever the shell left the cursor, so
    // the terminal scrolls while drawing it, and Konsole then only paints the
    // borders, not the text. Every later frame starts from the top-left, so
    // ask for one right away instead of waiting for the first mouse move.
    bool first_frame_drawn = false;

    auto document = ftxui::Renderer(panel_navigation, [&] {
        if (!first_frame_drawn) {
            first_frame_drawn = true;
            screen->PostEvent(ftxui::Event::Custom);
        }

        return ftxui::color(ftxui::Color::White,
            ftxui::vbox({
                top_bar_component->Render(),
                ftxui::flex(
                    constrained_split->Render()
                )
            })
        );
    });

    ftxui::Component complete_document = key_listener_function(*screen, document);

    // single file mode has no explorer: start in the editor, not on the top bar.
    if (arg_is_file) {
        tab_content->TakeFocus();
    }

    std::cout << "\x1b[?2004h" << std::flush;  // bracketed paste on
    screen->Loop(complete_document);
    std::cout << "\x1b[?2004l" << std::flush;  // and off again

    return 0;
}
