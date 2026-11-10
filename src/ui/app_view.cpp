#include "ui/app_view.hpp"

#include <algorithm>
#include <csignal>
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
#include "editor/commands.hpp"
#include "editor/mode.hpp"
#include "editor/paths.hpp"
#include "editor/recovery.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "platform/file_watcher.hpp"
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
#include "ui/window_grid.hpp"

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

    // a container keeps Left / Right for itself when its rows don't use them,
    // so they never throw the focus from a menu page into the explorer.
    class KeepSideArrows : public ftxui::ComponentBase {
    public:
        explicit KeepSideArrows(ftxui::Component child) { Add(std::move(child)); }

        bool OnEvent(ftxui::Event event) override {
            if (ftxui::ComponentBase::OnEvent(event)) {
                return true;
            }
            return event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight ||
                   event == ftxui::Event::Character('h') || event == ftxui::Event::Character('l');
        }
    };

    // the row at the bottom of the main view: the command line while typing
    // a command, else a message (until the next key), else the status bar.
    ftxui::Element bottom_row(const ftxui::Component& command_line_component) {
        const bool message = !command_error.empty() || !command_output.empty();
        if (mode == Mode::Command || message || !settings.show_status_bar) {
            return command_line_area(command_line_component);
        }
        return status_bar_element();
    }

    // the terminal's window was closed (SIGHUP) or the editor was asked to
    // end (SIGTERM). FTXUI catches both, puts the terminal back as it was,
    // then hands the signal to the handler that was there before it: this
    // one, called from FTXUI's own code and not in the middle of anything.
    // The unsaved changes are kept for the next start, then the signal does
    // what it always does.
    void keep_changes_then_end(int signal_number) {
        const int kept = keep_all_recovery_copies();
        if (kept > 0) {
            std::cerr << "Itahi was closed with unsaved changes: " << kept << (kept == 1 ? " file was" : " files were")
                      << " kept. Start it again and type >recover to bring them back." << std::endl;
        }
        std::signal(signal_number, SIG_DFL);
        std::raise(signal_number);
    }

}  // namespace

int run_app(const AppOptions& options) {
    ftxui::ScreenInteractive screen_instance = ftxui::ScreenInteractive::Fullscreen();
    screen = &screen_instance;

    int left_panel_width = std::clamp(settings.explorer_width, min_explorer_width, max_explorer_width);
    const bool has_explorer = !options.single_file;

    std::shared_ptr<WindowGrid> windows = make_window_grid();
    GitRefresher git(options.folder.string());

    // created further down, once the containers it gives the focus back to exist.
    ftxui::Component command_line_component;

    // --- the main view: the tabs, the windows, the bottom row ---
    // the windows first: the first child is the one the keys go to, and the
    // tab bar only takes clicks (it is drawn above them all the same).
    ftxui::Component tab_bar = make_tab_bar();
    ftxui::Component main_view_container = ftxui::Container::Vertical({windows, tab_bar});
    ftxui::Component main_view = ftxui::Renderer(main_view_container, [&] {
        ftxui::Elements rows;
        if (!workspace.documents().empty() && settings.show_tabs) {
            rows.push_back(tab_bar->Render());
        }
        rows.push_back(ftxui::flex(windows->Render()));
        rows.push_back(bottom_row(command_line_component));
        return ftxui::flex(ftxui::border(ftxui::vbox(std::move(rows))));
    });

    // --- the menu tabs, one page each ---
    ftxui::Component file_back_button_container = ftxui::Container::Horizontal({file_back_button_func()});
    ftxui::Component tab_content = ftxui::Container::Tab({
        main_view,
        make_file_page(),
        make_edit_page([windows]() -> EditorView& { return windows->active_editor(); }),
        make_view_page(),
        make_settings_page(),
        make_help_page(),
    }, &tab_selected);

    // --- the explorer ---
    ExplorerPanel explorer;
    if (has_explorer) {
        explorer = make_explorer_panel(options.folder, left_panel_width);
    }

    // the folders on screen, the ones the open files are in and Git's own are
    // watched: what another program changes in them shows up by itself.
    std::unique_ptr<FileWatcher> watcher;
    auto watch_folders = [&] {
        if (!watcher) {
            return;
        }
        std::vector<std::string> folders = has_explorer ? explorer.shown_folders() : std::vector<std::string>{};
        for (const auto& document : workspace.documents()) {
            const std::string folder = fs::path(document->path()).parent_path().string();
            folders.push_back(folder.empty() ? "." : folder);
        }
        if (git_status.in_repository && !git_status.top_level.empty()) {
            folders.push_back(git_status.top_level + "/.git");
        }
        std::sort(folders.begin(), folders.end());
        folders.erase(std::unique(folders.begin(), folders.end()), folders.end());
        watcher->watch(folders);
    };

    // a file was created (>e, New File, >w name) or F5: list the folders again.
    hooks.refresh_explorer = [&] {
        if (has_explorer) {
            explorer.refresh();
        }
        watch_folders();
        screen->PostEvent(ftxui::Event::Custom);
    };
    hooks.refresh_git = [&git] { git.request(); };

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

    static const char* const tab_names[] = {"", "File", "Edit", "View", "Settings", "Help"};
    ftxui::Component main_content = ftxui::Renderer(main_content_container, [&] {
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
        const char* name = tab_names[std::clamp(tab_selected, 0, 5)];
        return ftxui::border(ftxui::vbox({
            ftxui::hbox({ftxui::text(" "), file_back_button_container->Render(), ftxui::text(" "), ftxui::separator(),
                         ftxui::text("  "), ftxui::bold(ftxui::text(name)),
                         ftxui::filler()}),
            ftxui::separator(),
            ftxui::flex(tab_content->Render()),
            command_line_area(command_line_component),
        }));
    });
    main_content = std::make_shared<KeepSideArrows>(main_content);

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
        // scrolls itself, a window scrolls its own text, a menu its rows.
        if (event.is_mouse()) {
            const ftxui::Mouse mouse = event.mouse();
            const bool over_explorer = has_explorer && explorer.contains(mouse.x, mouse.y);
            const bool wheel = mouse.button == ftxui::Mouse::WheelUp || mouse.button == ftxui::Mouse::WheelDown;
            if (!over_explorer && wheel && status == 1) {
                // the window gets it directly: FTXUI containers ignore keys
                // while they are not focused.
                const int rows = mouse.button == ftxui::Mouse::WheelUp ? -get_mouse_sensitivity() : get_mouse_sensitivity();
                windows->wheel(mouse.x, mouse.y, rows);
                return true;
            }
            // over a menu page: the page scrolls itself (ui/widgets.cpp).
        }

        const bool handled = body_split->OnEvent(event);
        left_panel_width = std::clamp(left_panel_width, min_explorer_width, max_explorer_width);
        // kept for the next run, once the drag is over (not on every step of it).
        if (!event.is_mouse() || event.mouse().motion == ftxui::Mouse::Released) {
            settings.explorer_width = left_panel_width;
        }
        return handled;
    });

    ftxui::Component top_bar_component = make_top_bar();
    auto main_container = ftxui::Container::Vertical({top_bar_component, constrained_split});

    // --- hooks for the editor layer, which doesn't know about FTXUI ---
    hooks.redraw = [] { screen->PostEvent(ftxui::Event::Custom); };
    hooks.quit = [] { screen->ExitLoopClosure()(); };

    // the folder new files go to (see editor/state.hpp): the explorer's
    // selected folder, or the folder of its selected file; else the open
    // file's folder; else the folder Itahi opened.
    hooks.active_folder = [&] {
        const std::string selected = has_explorer ? explorer.selected_path() : std::string();
        if (!selected.empty()) {
            return explorer.selected_is_folder() ? selected : fs::path(selected).parent_path().string();
        }
        if (workspace.active()) {
            return fs::path(workspace.active()->path()).parent_path().string();
        }
        return workspace.root;
    };
    // >rename and >rm without a path: the explorer's row, when the explorer
    // had the focus (now, or when the command line opened).
    hooks.command_target = [&] {
        if (!has_explorer) {
            return std::string();
        }
        const bool from_explorer = mode == Mode::Command ? focus_before_command_mode == explorer.rows : explorer.rows->Focused();
        return from_explorer ? explorer.selected_path() : std::string();
    };

    // opening a file (or a search) closes any menu and gives the editor the focus.
    // (Also once the command line closes: a command typed from the explorer
    // that opens a file leaves the focus in that file.)
    hooks.show_editor = [&] {
        status = 1;
        tab_selected = 0;
        focus_before_command_mode = tab_content;
        tab_content->TakeFocus();
    };
    hooks.show_help = [&] {
        status = 2;
        tab_selected = 5;
        focus_before_command_mode = tab_content;
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
        windows,
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

    ftxui::Component listener = key_listener_function(*screen, document, [windows] { return windows->active_editor().copy_selection(); });

    // a setting changed (View or Settings tab): write the config file.
    ftxui::Component app = std::make_shared<AfterEvents>(listener, [&options] {
        std::string error;
        if (!save_config_if_changed(options.config_path, error)) {
            command_error = error;
        }
    });

    // start where the work is: in the file when one was given, else on the
    // explorer's first row (not on the menu bar).
    if (options.single_file || !explorer.rows->Focusable()) {
        tab_content->TakeFocus();
    } else {
        explorer.rows->TakeFocus();
    }

    // unsaved changes kept from a run that ended badly: say so on the first frame.
    if (command_error.empty()) {
        const std::string root = absolute_normal(workspace.root);
        int kept = 0;
        for (const RecoveryCopy& copy : recovery_copies()) {
            kept += copy.path.compare(0, root.size() + 1, root + "/") == 0 ? 1 : 0;
        }
        if (kept > 0) {
            command_output = "Unsaved changes of " + std::to_string(kept) + (kept == 1 ? " file" : " files") +
                             " were kept from last time: >recover brings them back, >recover! forgets them";
        }
    }

    // what other programs change, and the work done once a second, are
    // handed to this thread: the watcher has its own.
    watcher = std::make_unique<FileWatcher>(
        [&] {
            screen->Post([&] {
                if (has_explorer) {
                    explorer.refresh();
                }
                git.request();
                const std::string changed = check_files_on_disk();
                if (!changed.empty()) {
                    command_output = changed;
                }
                watch_folders();
                screen->PostEvent(ftxui::Event::Custom);
            });
        },
        [&] {
            screen->Post([&] {
                // files on disks that tell nothing (network folders) are looked at too.
                const std::string changed = check_files_on_disk();
                const std::string problem = update_recovery_copies();
                if (!changed.empty()) {
                    command_output = changed;
                }
                if (!problem.empty()) {
                    command_error = problem;
                }
                watch_folders();
                if (!changed.empty() || !problem.empty()) {
                    screen->PostEvent(ftxui::Event::Custom);
                }
            });
        });
    watch_folders();

    git.request();
    std::signal(SIGTERM, keep_changes_then_end);
    std::signal(SIGHUP, keep_changes_then_end);
    std::cout << "\x1b[?2004h" << std::flush;  // bracketed paste on
    screen->Loop(app);
    std::cout << "\x1b[?2004l" << std::flush;  // and off again
    watcher.reset();  // its thread posts to the screen: gone before the screen is

    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGHUP, SIG_DFL);

    // the hooks point into this function: nothing may call them any more.
    hooks = Hooks{};
    open_command_line = nullptr;
    screen = nullptr;
    return 0;
}
