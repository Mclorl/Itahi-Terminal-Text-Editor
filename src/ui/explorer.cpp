#include "ui/explorer.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/string.hpp>

#include "core/file_io.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "syntax/theme.hpp"
#include "ui/ui_state.hpp"
#include "ui/widgets.hpp"

namespace fs = std::filesystem;

namespace {

    // whether the name shown for a path had to be cut (see truncate_name).
    std::unordered_map<std::string, bool>& name_was_cut() {
        static std::unordered_map<std::string, bool> cut;
        return cut;
    }

}  // namespace

std::string truncate_name(const std::string& name, int available_width, const std::string& path) {
    if (available_width <= 0) {
        return {};
    }

    // measured in terminal cells, not bytes: "日本" is 6 bytes but 4 cells.
    if (ftxui::string_width(name) <= available_width) {
        name_was_cut()[path] = false;
        return name;
    }

    name_was_cut()[path] = true;

    const std::string suffix = "...";
    if (available_width <= static_cast<int>(suffix.size())) {
        return suffix.substr(0, static_cast<std::size_t>(available_width));
    }

    // never cut inside a character.
    const int room = available_width - static_cast<int>(suffix.size());
    const std::vector<std::string> glyphs = ftxui::Utf8ToGlyphs(name);
    std::string out;
    int used = 0;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        if (glyphs[i].empty()) {
            continue;  // 2nd cell of a full-width glyph
        }
        const int width = i + 1 < glyphs.size() && glyphs[i + 1].empty() ? 2 : 1;
        if (used + width > room) {
            break;
        }
        out += glyphs[i];
        used += width;
    }
    return out + suffix;
}

namespace {

    int available_name_width(int panel_width, int indentation_level) {
        constexpr int border_width = 4;
        constexpr int indentation_width = 1;
        constexpr int scrollbar_width = 1;
        constexpr int git_mark_width = 2;  // " M" on the right

        return std::max(1, panel_width - border_width - scrollbar_width - git_mark_width - indentation_level * indentation_width);
    }

    // the entries of a folder; a folder that can't be read shows its error
    // on the command line instead of crashing the editor.
    std::vector<fs::path> folder_entries(const fs::path& folder) {
        std::string error;
        std::vector<fs::path> entries = list_directory(folder, error);
        if (!error.empty()) {
            command_error = error;
        }
        return entries;
    }

    // Git's mark on the right of a row: what changed, like vscode.
    ftxui::Element git_mark(const std::string& path, bool folder) {
        if (!git_status.in_repository) {
            return ftxui::text("");
        }
        if (folder) {
            const char state = git_status.state_of(path);
            if (state == '?') {
                return ftxui::color(ftxui::Color::GreenLight, ftxui::text(" U"));
            }
            return git_status.changes_under(path) ? ftxui::color(ftxui::Color::Gold1, ftxui::text(" •")) : ftxui::text("");
        }
        switch (git_status.state_of(path)) {
            case 'M': return ftxui::color(ftxui::Color::Gold1, ftxui::text(" M"));
            case 'A': return ftxui::color(ftxui::Color::GreenLight, ftxui::text(" A"));
            case '?': return ftxui::color(ftxui::Color::GreenLight, ftxui::text(" U"));
            case 'D': return ftxui::color(ftxui::Color::RedLight, ftxui::text(" D"));
            case 'R': return ftxui::color(ftxui::Color::SkyBlue1, ftxui::text(" R"));
            case 'U': return ftxui::color(ftxui::Color::Red, ftxui::text(" !"));
            default: return ftxui::text("");
        }
    }

    // what the editor shows for a file row: the name, "*" when unsaved,
    // yellow when unsaved and not open, the theme's color when open, dim when
    // Git ignores it, and Git's mark on the right.
    ftxui::Element file_row_element(const std::string& path, const std::string& label, bool focused) {
        const Document* document = workspace.find(path);
        const bool unsaved = document && document->unsaved();
        const bool is_open_in_editor = active_path() == path;

        std::string display_name = label;
        // a cut name already ends in "..."; drop one dot to make room for "*".
        if (unsaved && name_was_cut()[path] && !display_name.empty()) {
            display_name.pop_back();
        }

        ftxui::Element e = ftxui::text("  " + display_name + (unsaved ? "*" : ""));
        if (is_open_in_editor) {
            // the opened file keeps its color even when the explorer is not
            // focused, so the user can still see it while editing.
            e = ftxui::color(current_theme().open_file, e);
        } else if (unsaved) {
            e = ftxui::color(ftxui::Color::Yellow1, e);
        } else if (git_status.ignored(path)) {
            e = ftxui::dim(e);
        }
        e = ftxui::hbox({e, ftxui::filler(), git_mark(path, false)});

        // remember this row's screen box so the view can be scrolled to it
        // independently of the selection.
        e = e | capture_box(left_panel_row_boxes[path]);

        return focused ? ftxui::bold(e) : e;
    }

    // adds a button for every regular file in `file_paths` to `container`.
    void add_file_rows(ftxui::Component container, const std::vector<fs::path>& file_paths, int& panel_width, int indentation) {
        for (const fs::path& p : file_paths) {
            std::error_code ec;
            if (!fs::is_regular_file(fs::status(p, ec)) || ec) {
                continue;
            }

            const std::string display_name = truncate_name(p.filename().string(), available_name_width(panel_width, indentation), p.string());

            ftxui::ButtonOption option = ftxui::ButtonOption::Simple();

            // Raw pointer to the container owning this button. A raw pointer is used
            // on purpose: capturing the shared_ptr would create a reference cycle
            // (container -> button -> transform -> container). The container always
            // outlives its children, so this is safe.
            ftxui::ComponentBase* owner = container.get();

            option.transform = [path = p.string(), owner](const ftxui::EntryState& current_state) {
                // Only the row that genuinely owns the selection updates the cache.
                // `active` is used instead of `focused`, because FTXUI reports
                // `focused` for hovered rows too; combined with `owner->Focused()`
                // this means hovering the explorer while the focus is in the main
                // content no longer overwrites the cached selection.
                if (current_state.active && owner->Focused() && left_panel_selected_file_path != path) {
                    left_panel_selected_file_path = path;
                    explorer_selected_path = path;
                    explorer_selected_is_folder = false;
                    // the view follows the selection using the row boxes measured
                    // during layout, ie. one frame late: ask for that one extra
                    // frame. (Only on a change: posting on every render made the
                    // app redraw non-stop at 100% CPU.)
                    screen->PostEvent(ftxui::Event::Custom);
                }
                return file_row_element(path, current_state.label, current_state.focused);
            };

            auto button = ftxui::Button(display_name, [p]() {
                std::string error;
                if (!workspace.open(p, error)) {
                    command_error = error;  // an unreadable file must never look empty
                    return;
                }
                if (show_editor) {
                    show_editor();
                }
                if (screen) {
                    screen->PostEvent(ftxui::Event::Custom);
                }
            }, option);

            container->Add(button);
        }
    }

    // adds a folder row (and its children while open) for every folder in `file_paths` to `parent_container`.
    void add_folder_rows(const std::vector<fs::path>& file_paths, ftxui::Component parent_container, int& panel_width, int indentation) {
        for (const fs::path& p : file_paths) {
            std::error_code ec;
            if (!fs::is_directory(fs::status(p, ec)) || ec) {
                continue;
            }
            if (p.filename() == ".git") {
                continue;  // never something you want to browse in an editor
            }
            const std::string path = p.string();

            const std::string display_name = truncate_name(p.filename().string(), available_name_width(panel_width, indentation), path);

            // separate container for each folder
            auto children_container = ftxui::Container::Vertical({});

            auto visible_children = ftxui::Maybe(children_container, [path] {
                return workspace.opened_folders.count(path) != 0;
            });

            auto folder_component = ftxui::Container::Vertical({});

            ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
            // raw pointer for the same reason as for the file rows: no reference cycle.
            ftxui::ComponentBase* owner = folder_component.get();

            option.transform = [path, owner](const ftxui::EntryState& current_state) {
                // the folder row owns the selection (keyboard or click): remember
                // it for New File.
                if (current_state.active && owner->Focused() &&
                    (explorer_selected_path != path || !explorer_selected_is_folder)) {
                    explorer_selected_path = path;
                    explorer_selected_is_folder = true;
                }

                const bool is_open = workspace.opened_folders.count(path) != 0;
                const std::string prefix = is_open ? "v " : "> ";

                ftxui::Element e = ftxui::text(prefix + current_state.label);
                if (workspace.unsaved_under(path)) {
                    e = ftxui::color(ftxui::Color::Yellow1, e);
                } else if (git_status.ignored(path)) {
                    e = ftxui::dim(e);
                }
                e = ftxui::hbox({e, ftxui::filler(), git_mark(path, true)});
                return current_state.focused ? ftxui::bold(e) : e;
            };

            // fills the folder's rows from disk.
            auto fill_children = [path, children_container, &panel_width, indentation] {
                children_container->DetachAllChildren();
                const std::vector<fs::path> folder_content = folder_entries(path);
                add_folder_rows(folder_content, children_container, panel_width, indentation + 1);
                add_file_rows(children_container, folder_content, panel_width, indentation + 1);
            };

            auto button = ftxui::Button(display_name, [path, fill_children]() {
                if (workspace.opened_folders.count(path)) {
                    workspace.opened_folders.erase(path);
                } else {
                    workspace.opened_folders.insert(path);
                    fill_children();
                }
                screen->PostEvent(ftxui::Event::Custom);
            }, option);

            // after resizing, reconstruct its children immediately.
            if (workspace.opened_folders.count(path) != 0) {
                fill_children();
            }

            // render the subfolder with indentation.
            auto indented_children = ftxui::Renderer(visible_children, [visible_children, path, button] {
                if (workspace.opened_folders.count(path) == 0) {
                    return ftxui::emptyElement();
                }

                auto separator = ftxui::color(ftxui::Color::RGB(150, 150, 150), ftxui::separator());
                if (!button->Focused()) {
                    separator = ftxui::color(ftxui::Color::RGB(80, 80, 80), ftxui::separator());
                }

                // the rows take the panel's whole width, so Git's marks stay on its right edge.
                return ftxui::hbox({separator, visible_children->Render() | ftxui::xflex});
            });

            folder_component->Add(button);
            folder_component->Add(indented_children);

            parent_container->Add(folder_component);
        }
    }

    // what the panel keeps between frames.
    struct PanelState {
        fs::path root;
        std::vector<fs::path> entries;   // the root folder's entries
        int previous_width = 0;
        // the explorer's viewport: the wheel moves it by whole rows; moving the
        // selection scrolls it only as far as needed to keep the row visible.
        ScrollState scroll;
        bool previous_focused = false;
        std::string previous_selected;
        // screen region of the explorer, and the full (possibly taller than the
        // view) box of its rows, used with the row boxes to follow the selection.
        ftxui::Box panel_box;
        ftxui::Box content_box;
    };

}  // namespace

ExplorerPanel make_explorer_panel(const fs::path& root, int& width) {
    auto state = std::make_shared<PanelState>();
    state->root = root;
    state->previous_width = width;
    std::string error;
    state->entries = list_directory(root, error);

    ExplorerPanel explorer;
    explorer.rows = ftxui::Container::Vertical({});
    ftxui::Component rows = explorer.rows;

    auto fill = [state, rows, &width] {
        rows->DetachAllChildren();
        add_folder_rows(state->entries, rows, width, 0);
        add_file_rows(rows, state->entries, width, 0);
    };
    fill();

    explorer.refresh = [state, fill] {
        std::string list_error;
        state->entries = list_directory(state->root, list_error);
        if (!list_error.empty()) {
            command_error = list_error;
        }
        fill();
    };

    explorer.contains = [state](int x, int y) { return state->panel_box.Contain(x, y); };

    auto panel = ftxui::Renderer(rows, [state, rows, fill, &width] {
        if (width != state->previous_width) {
            // names are cut to the panel width when the rows are built, so a
            // new width needs new rows.
            state->previous_width = width;
            fill();
        }

        ftxui::Element entries = rows->Render() | capture_box(state->content_box);
        const bool focused = rows->Focused();

        // the selection changed (keyboard or click), or the explorer just got
        // the focus: scroll just enough to show the selected row. Row boxes
        // are from the last frame, which is fine: rows don't move on their own.
        const bool follow = focused && (!state->previous_focused || left_panel_selected_file_path != state->previous_selected);
        state->previous_focused = focused;
        state->previous_selected = left_panel_selected_file_path;
        if (follow) {
            const auto it = left_panel_row_boxes.find(left_panel_selected_file_path);
            if (it != left_panel_row_boxes.end()) {
                state->scroll.keep_visible(it->second.y_min - state->content_box.y_min);
            }
        }

        return ftxui::border(ftxui::vbox({
                   ftxui::bold(ftxui::text("Explorer")),
                   ftxui::separator(),
                   scroll_frame(white_vscroll_indicator(entries), state->scroll) | ftxui::flex,
               })) |
               ftxui::reflect(state->panel_box) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width);
    });

    // the wheel over the explorer scrolls its view by whole rows, and never
    // moves the selection or the focus.
    explorer.panel = ftxui::CatchEvent(panel, [state](ftxui::Event event) {
        if (!event.is_mouse()) {
            return false;
        }
        const ftxui::Mouse& mouse = event.mouse();
        if (mouse.button != ftxui::Mouse::WheelUp && mouse.button != ftxui::Mouse::WheelDown) {
            return false;
        }
        if (!state->panel_box.Contain(mouse.x, mouse.y)) {
            return false;  // over the editor: not for the explorer
        }
        const int rows_per_notch = get_mouse_sensitivity();
        state->scroll.scroll_by(mouse.button == ftxui::Mouse::WheelUp ? -rows_per_notch : rows_per_notch);
        screen->PostEvent(ftxui::Event::Custom);
        return true;
    });
    return explorer;
}
