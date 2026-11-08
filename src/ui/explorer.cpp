#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/string.hpp>

#include "ui/explorer.hpp"

#include "ui/widgets.hpp"
#include "core/file_io.hpp"
#include "editor/workspace.hpp"
#include "editor/state.hpp"
#include "ui/ui_state.hpp"

namespace fs = std::filesystem;

std::string truncate_name(const std::string& name, int available_width, const std::string& path) {
    if (available_width <= 0) {
        return {};
    }

    // measured in terminal cells, not bytes: "日本" is 6 bytes but 4 cells.
    if (ftxui::string_width(name) <= available_width) {
        file_truncate_name_bool_status[path] = false;
        return name;
    }

    file_truncate_name_bool_status[path] = true;

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

        return std::max(1, panel_width - border_width - scrollbar_width - indentation_level * indentation_width);
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

    // what the editor shows for a file row: the name, "*" when unsaved,
    // yellow when unsaved and not open, orange when open.
    ftxui::Element file_row_element(const std::string& path, const std::string& label, bool focused) {
        const bool unsaved = file_unsaved_status[path];
        const bool is_open_in_editor = long_active_file_name == path;

        std::string display_name = label;
        // a cut name already ends in "..."; drop one dot to make room for "*".
        if (unsaved && file_truncate_name_bool_status[path] && !display_name.empty()) {
            display_name.pop_back();
        }

        ftxui::Element e = ftxui::text("  " + display_name + (unsaved ? "*" : ""));
        if (is_open_in_editor) {
            // the opened file stays orange even when the explorer is not
            // focused, so the user can still see it while editing.
            e = ftxui::color(ftxui::Color::Orange1, e);
        } else if (unsaved) {
            e = ftxui::color(ftxui::Color::Yellow1, e);
        }

        // remember this row's screen box so the view can be scrolled to it
        // independently of the selection (see main.cpp).
        e = e | capture_box(left_panel_row_boxes[path]);

        return focused ? ftxui::bold(e) : e;
    }

}  // namespace

// opens `path` in the editor. Returns false (with command_error set) when
// the file can't be read: an unreadable file must never look empty.
void opened_directory_file_entry(ftxui::Component container, const std::vector<fs::path>& file_paths,
                                 std::string &content_main_user_updated, std::string &active_file_local,
                                 std::string &long_active_file_name_local, int &left_panel_width, int indentation) {
    for (const fs::path& p : file_paths) {
        std::error_code ec;
        if (!fs::is_regular_file(fs::status(p, ec)) || ec) {
            continue;
        }

        const std::string display_name = truncate_name(p.filename().string(), available_name_width(left_panel_width, indentation), p.string());

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

        auto button = ftxui::Button(display_name, [p, &content_main_user_updated, &long_active_file_name_local, &active_file_local]() {
            if (!open_file(p, content_main_user_updated, active_file_local, long_active_file_name_local)) {
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

void opened_directory_entry(ftxui::ScreenInteractive *screen_local, const std::vector<fs::path>& file_paths,
                            std::unordered_set<std::string>& opened_folders_local, std::string &content_local,
                            std::string &active_file_local, std::string &long_active_file_name_local,
                            ftxui::Component parent_container, int &left_panel_width, int indentation) {
    for (const fs::path& p : file_paths) {
        std::error_code ec;
        if (!fs::is_directory(fs::status(p, ec)) || ec) {
            continue;
        }
        if (p.filename() == ".git") {
            continue;  // never something you want to browse in an editor
        }
        const std::string path = p.string();

        const std::string display_name = truncate_name(p.filename().string(), available_name_width(left_panel_width, indentation), path);

        // separate container for each folder
        auto children_container = ftxui::Container::Vertical({});

        auto visible_children = ftxui::Maybe(children_container, [&opened_folders_local, path] {
            return opened_folders_local.count(path) != 0;
        });

        auto folder_component = ftxui::Container::Vertical({});

        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        // raw pointer for the same reason as for the file rows: no reference cycle.
        ftxui::ComponentBase* owner = folder_component.get();

        option.transform = [path, &opened_folders_local, owner](const ftxui::EntryState& current_state) {
            // the folder row owns the selection (keyboard or click): remember
            // it for New File.
            if (current_state.active && owner->Focused() &&
                (explorer_selected_path != path || !explorer_selected_is_folder)) {
                explorer_selected_path = path;
                explorer_selected_is_folder = true;
            }

            const bool is_open = opened_folders_local.count(path) != 0;
            const std::string prefix = is_open ? "v " : "> ";

            ftxui::Element e = ftxui::text(prefix + current_state.label);
            if (check_content_directory_for_unsaved_file(path)) {
                e = ftxui::color(ftxui::Color::Yellow1, e);
            }
            return current_state.focused ? ftxui::bold(e) : e;
        };

        // fills the folder's rows from disk.
        auto fill_children = [screen_local, path, &opened_folders_local, &content_local, &active_file_local,
                              &long_active_file_name_local, children_container, &left_panel_width, indentation] {
            children_container->DetachAllChildren();
            const std::vector<fs::path> folder_content = folder_entries(path);
            opened_directory_entry(screen_local, folder_content, opened_folders_local, content_local, active_file_local,
                                   long_active_file_name_local, children_container, left_panel_width, indentation + 1);
            opened_directory_file_entry(children_container, folder_content, content_local, active_file_local,
                                        long_active_file_name_local, left_panel_width, indentation + 1);
        };

        auto button = ftxui::Button(display_name, [screen_local, path, &opened_folders_local, fill_children]() {
            if (opened_folders_local.count(path)) {
                opened_folders_local.erase(path);
            } else {
                opened_folders_local.insert(path);
                fill_children();
            }
            screen_local->PostEvent(ftxui::Event::Custom);
        }, option);

        // after resizing, reconstruct its children immediately.
        if (opened_folders_local.count(path) != 0) {
            fill_children();
        }

        // render the subfolder with indentation.
        auto indented_children = ftxui::Renderer(visible_children, [visible_children, &opened_folders_local, path, button] {
            if (opened_folders_local.count(path) == 0) {
                return ftxui::emptyElement();
            }

            auto separator = ftxui::color(ftxui::Color::RGB(150, 150, 150), ftxui::separator());
            if (!button->Focused()) {
                separator = ftxui::color(ftxui::Color::RGB(80, 80, 80), ftxui::separator());
            }

            return ftxui::hbox({separator, visible_children->Render()});
        });

        folder_component->Add(button);
        folder_component->Add(indented_children);

        parent_container->Add(folder_component);
    }
}
