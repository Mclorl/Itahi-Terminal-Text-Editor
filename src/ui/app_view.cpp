#include "ui/app_view.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "core/version.hpp"
#include "editor/config.hpp"
#include "editor/mode.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "platform/git.hpp"
#include "ui/command_line.hpp"
#include "ui/editor_view.hpp"
#include "ui/explorer.hpp"
#include "ui/key_listener.hpp"
#include "ui/key_router.hpp"
#include "ui/menu_bar.hpp"
#include "ui/menu_pages.hpp"
#include "ui/status_bar.hpp"
#include "ui/tab_bar.hpp"
#include "ui/ui_state.hpp"
#include "ui/widgets.hpp"

namespace fs = std::filesystem;

namespace {

    // runs `after` once every event has been handled (the settings are saved
    // there when one changed).
    class AfterEvents : public ftxui::ComponentBase {
    public:
        AfterEvents(ftxui::Component child, std::function<void()> after) : after_(std::move(after)) {
            Add(std::move(child));
        }

        bool OnEvent(ftxui::Event event) override {
            const bool handled = ftxui::ComponentBase::OnEvent(event);
            after_();
            return handled;
        }

    private:
        std::function<void()> after_;
    };

    // asks Git for the status on a thread of its own, so a big repository
    // never makes the editor wait; the answer is handed to the UI thread.
    class GitRefresher {
    public:
        explicit GitRefresher(std::string folder) : folder_(std::move(folder)) {}
        GitRefresher(const GitRefresher&) = delete;
        GitRefresher& operator=(const GitRefresher&) = delete;
        ~GitRefresher() {
            if (worker_.joinable()) {
                worker_.join();
            }
        }

        // called on the UI thread only.
        void request() {
            if (running_) {
                again_ = true;  // something changed while git ran: ask once more after it
                return;
            }
            if (worker_.joinable()) {
                worker_.join();  // the last run is over, only its thread is left
            }
            running_ = true;
            worker_ = std::thread([this] {
                GitStatus result = read_git_status(folder_);
                screen->Post([this, result = std::move(result)]() mutable {
                    git_status = std::move(result);
                    running_ = false;
                    if (again_) {
                        again_ = false;
                        request();
                    }
                    screen->PostEvent(ftxui::Event::Custom);
                });
            });
        }

    private:
        std::string folder_;
        std::thread worker_;
        bool running_ = false;
        bool again_ = false;
    };

}  // namespace

int run_app(const AppOptions& options) {
    ftxui::ScreenInteractive screen_instance = ftxui::ScreenInteractive::Fullscreen();
    screen = &screen_instance;

    int left_panel_width = 25;  // the explorer's width, changed by dragging the separator
    const bool has_explorer = !options.single_file;

    std::shared_ptr<EditorView> editor = make_editor_view();
    GitRefresher git(options.folder.string());

    // --- the menu tabs ---
    ftxui::Component file_back_button_container = ftxui::Container::Horizontal({file_back_button_func()});
    ftxui::Component file_tab_container = ftxui::Container::Vertical({
        file_btn_new_file_func(),
        file_btn_save_file_func(),
        file_btn_close_file_func(),
    });
    ftxui::Component edit_tab_container = ftxui::Container::Vertical({
        edit_btn_undo_func(),
        edit_btn_redo_func(),
        edit_btn_cut_func(*editor),
        edit_btn_copy_func(*editor),
        edit_btn_paste_func(*editor),
        edit_btn_select_all_func(*editor),
    });
    ftxui::Component view_tab_container = ftxui::Container::Vertical({
        toggle_linenums_func(),
        toggle_underline_active_row_on_numbers_func(),
        toggle_underline_active_row_func(),
        active_row_style_row_func(),
        toggle_syntax_highlighting_func(),
        toggle_word_wrap_func(),
        toggle_status_bar_func(),
        toggle_tabs_func(),
        theme_row_func(),
        syntax_color_settings_func(),
    });
    ftxui::Component settings_tab_container = ftxui::Container::Vertical({
        mouse_sens_row_func(),
        tab_size_row_func(),
        toggle_tab_inserts_spaces_func(),
        scroll_style_row_func(),
    });
    ftxui::Component help_tab_container = ftxui::Container::Vertical({
        warning_text_link_direct_func(),
        btn_about_func(),
        btn_ftxui_about_func(),
    });

    // created further down, once the containers it gives the focus back to exist.
    ftxui::Component command_line_component;

    // --- the main view: the editor, the binary file warning, or the greeting ---
    // "Open Anyway" for a binary / unsupported encoding file (like vscode).
    ftxui::Component open_anyway_button = ftxui::Button("Open Anyway", [] {
        if (Document* document = workspace.active()) {
            document->open_anyway();
        }
    }, create_plain_button_option());

    enum MainViewPage { EDITOR_PAGE, BINARY_WARNING_PAGE, GREETING_PAGE };
    int main_view_page = EDITOR_PAGE;
    ftxui::Component main_view_pages = ftxui::Container::Tab({
        editor,
        open_anyway_button,
        ftxui::Renderer([] { return ftxui::emptyElement(); }),  // greeting: nothing to focus
    }, &main_view_page);

    // the pages first: the first child is the one the keys go to, and the
    // tab bar only takes clicks (it is drawn above them all the same).
    ftxui::Component tab_bar = make_tab_bar();
    ftxui::Component main_view_container = ftxui::Container::Vertical({main_view_pages, tab_bar});

    ftxui::Component main_view = ftxui::Renderer(main_view_container, [&] {
        const Document* document = workspace.active();
        main_view_page = !document ? GREETING_PAGE : document->hidden_as_binary() ? BINARY_WARNING_PAGE : EDITOR_PAGE;

        ftxui::Element page;
        if (main_view_page == EDITOR_PAGE) {
            page = editor->Render();
        } else if (main_view_page == BINARY_WARNING_PAGE) {
            page = ftxui::vcenter(ftxui::vbox({
                ftxui::hcenter(ftxui::bold(ftxui::color(ftxui::Color::Yellow, ftxui::text("Warning")))),
                ftxui::separatorEmpty(),
                ftxui::paragraphAlignCenter("The file is not displayed in the text editor because it is either binary or uses an unsupported text encoding."),
                ftxui::dim(ftxui::paragraphAlignCenter("Opening it anyway could break the Itahi Text Editor.")),
                ftxui::separatorEmpty(),
                ftxui::hcenter(open_anyway_button->Render()),
            }));
        } else {
            page = ftxui::vcenter(ftxui::vbox({
                ftxui::hcenter(ftxui::bold(ftxui::text("Welcome to Itahi Text Editor" + ITAHI_VERSION))),
                ftxui::separatorEmpty(),
                ftxui::dim(ftxui::paragraphAlignCenter("Select a file in the Explorer panel on the left to start editing.")),
            }));
        }

        ftxui::Elements rows;
        if (document && settings.show_tabs) {
            rows.push_back(tab_bar->Render());
        }
        rows.push_back(ftxui::flex(page));
        // one row at the bottom: the command line while typing a command, else a
        // message (until the next key), else the status bar.
        const bool message = !command_error.empty() || !command_output.empty();
        if (mode == Mode::Command || message || !settings.show_status_bar) {
            rows.push_back(command_line_area(command_line_component));
        } else {
            rows.push_back(status_bar_element());
        }
        return ftxui::flex(ftxui::border(ftxui::vbox(std::move(rows))));
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

    // --- the explorer ---
    ExplorerPanel explorer;
    if (has_explorer) {
        explorer = make_explorer_panel(options.folder, left_panel_width);
    }

    // a file was created (>e, New File, >w name) or F5: list the folders again.
    request_explorer_refresh = [&] {
        if (has_explorer) {
            explorer.refresh();
        }
        screen->PostEvent(ftxui::Event::Custom);
    };
    request_git_refresh = [&git] { git.request(); };

    // component that had the focus before command mode, given it back when
    // the command line closes.
    ftxui::Component focus_before_command_mode = tab_content;
    ftxui::Component main_content_container;  // used by the command line below

    command_line_component = ftxui::Maybe(
        command_line_input([&] {
            set_normal_mode();
            command_line.clear();
            command_error.clear();
            // the hidden command line must not stay the active child.
            main_content_container->SetActiveChild(tab_content.get());
            focus_before_command_mode->TakeFocus();
        }),
        [] { return mode == Mode::Command; });

    main_content_container = ftxui::Container::Vertical({
        file_back_button_container,
        tab_content,
        command_line_component,
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
        return ftxui::border(ftxui::vbox({
            ftxui::hbox({file_back_button_container->Render(), ftxui::separator(), ftxui::bold(ftxui::text("Menu Options:"))}),
            ftxui::separator(),
            ftxui::flex(tab_content->Render()),
            command_line_area(command_line_component),
        }));
    });

    ftxui::Component body_split = main_content;
    if (has_explorer) {
        // the explorer on the left, with a "⋮" grip on the separator (like vscode).
        ftxui::ResizableSplitOption split_option;
        split_option.main = explorer.panel;
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
        // the mouse wheel goes to what is under the pointer: the explorer
        // scrolls itself, everything else is the editor (or a menu).
        if (event.is_mouse()) {
            const ftxui::Mouse mouse = event.mouse();
            const bool over_explorer = has_explorer && explorer.contains(mouse.x, mouse.y);
            const bool wheel = mouse.button == ftxui::Mouse::WheelUp || mouse.button == ftxui::Mouse::WheelDown;
            if (!over_explorer && wheel) {
                const int rows = mouse.button == ftxui::Mouse::WheelUp ? -get_mouse_sensitivity() : get_mouse_sensitivity();
                if (status == 1) {
                    // the editor gets it directly: FTXUI containers ignore keys
                    // while they are not focused.
                    if (main_view_page == EDITOR_PAGE) {
                        editor->wheel(rows);
                    }
                    return true;
                }
                const ftxui::Event arrow = rows < 0 ? ftxui::Event::ArrowUp : ftxui::Event::ArrowDown;
                for (int i = 0; i < std::abs(rows); i++) {
                    main_content->OnEvent(arrow);
                }
                return true;
            }
        }

        const bool handled = body_split->OnEvent(event);
        constexpr int min_left_width = 25;
        constexpr int max_left_width = 100;
        left_panel_width = std::clamp(left_panel_width, min_left_width, max_left_width);
        return handled;
    });

    ftxui::Component top_bar_component = make_top_bar();
    auto main_container = ftxui::Container::Vertical({top_bar_component, constrained_split});

    // --- hooks for the editor layer, which doesn't know about FTXUI ---
    request_redraw = [] { screen->PostEvent(ftxui::Event::Custom); };
    request_quit = [] { screen->ExitLoopClosure()(); };

    // the folder new files go to (see editor/state.hpp): the explorer's
    // selected folder, or the folder of its selected file; else the open
    // file's folder; else the folder Itahi opened.
    active_folder = [] {
        if (!explorer_selected_path.empty()) {
            return explorer_selected_is_folder ? explorer_selected_path
                                               : fs::path(explorer_selected_path).parent_path().string();
        }
        if (workspace.active()) {
            return fs::path(workspace.active()->path()).parent_path().string();
        }
        return workspace.root;
    };

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
        } else if (has_explorer && explorer.rows->Focused()) {
            focus_before_command_mode = explorer.rows;
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

    ftxui::Component panel_navigation = make_key_router(main_container, {
        top_bar_component,
        has_explorer ? explorer.rows : nullptr,
        tab_content,
        editor,
        start_command_line,
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
        return ftxui::color(ftxui::Color::White, ftxui::vbox({
            top_bar_component->Render(),
            ftxui::flex(constrained_split->Render()),
        }));
    });

    ftxui::Component listener = key_listener_function(*screen, document, [editor] { return editor->copy_selection(); });

    // a setting changed (View or Settings tab): write the config file.
    ftxui::Component app = std::make_shared<AfterEvents>(listener, [&options] {
        std::string error;
        if (!save_config_if_changed(options.config_path, error)) {
            command_error = error;
        }
    });

    // single file mode has no explorer: start in the editor, not on the top bar.
    if (options.single_file) {
        tab_content->TakeFocus();
    }

    git.request();
    std::cout << "\x1b[?2004h" << std::flush;  // bracketed paste on
    screen->Loop(app);
    std::cout << "\x1b[?2004l" << std::flush;  // and off again

    // the hooks point into this function: nothing may call them any more.
    request_redraw = nullptr;
    request_quit = nullptr;
    show_editor = nullptr;
    request_explorer_refresh = nullptr;
    request_git_refresh = nullptr;
    active_folder = nullptr;
    open_command_line = nullptr;
    return 0;
}
