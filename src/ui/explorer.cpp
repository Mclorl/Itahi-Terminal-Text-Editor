#include "ui/explorer.hpp"

#include <algorithm>
#include <memory>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/string.hpp>

#include "core/file_io.hpp"
#include "editor/paths.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "syntax/theme.hpp"
#include "ui/command_line.hpp"
#include "ui/ui_state.hpp"
#include "ui/widgets.hpp"

namespace fs = std::filesystem;

std::string truncate_name(const std::string& name, int available_width) {
    if (available_width <= 0) {
        return {};
    }
    // measured in terminal cells, not bytes: "日本" is 6 bytes but 4 cells.
    if (ftxui::string_width(name) <= available_width) {
        return name;
    }
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

    using ftxui::Event;

    // after the folder Ctrl+N makes new files in.
    const char* const new_files_mark = " +";
    const int new_files_mark_cells = 2;

    struct Row {
        std::string path;
        std::string name;
        int depth = 0;
        bool folder = false;
    };

    // writes `text` from column `x` (not past `x_end`); returns the column after it.
    int put(ftxui::Screen& screen, int x, int y, int x_end, const std::string& text, ftxui::Color color, bool bold, bool dim) {
        for (const std::string& glyph : ftxui::Utf8ToGlyphs(text)) {
            if (x >= x_end) {
                break;
            }
            ftxui::Cell& cell = screen.CellAt(x, y);
            cell.character = glyph;
            cell.foreground_color = color;
            cell.bold = bold;
            cell.dim = dim;
            ++x;
        }
        return x;
    }

    class ExplorerView : public ftxui::ComponentBase {
    public:
        explicit ExplorerView(fs::path root) : root_(std::move(root)) { rebuild(); }

        bool Focusable() const override { return !rows_.empty(); }

        const std::string& selected_path() const {
            static const std::string none;
            return chosen_ && selected_ < rows_.size() ? rows_[selected_].path : none;
        }
        bool selected_is_folder() const { return chosen_ && selected_ < rows_.size() && rows_[selected_].folder; }
        bool contains(int x, int y) const { return panel_box.Contain(x, y); }

        std::vector<std::string> shown_folders() const {
            std::vector<std::string> folders{root_.string()};
            for (const Row& row : rows_) {
                if (row.folder && workspace.opened_folders.count(row.path) != 0) {
                    folders.push_back(row.path);
                }
            }
            return folders;
        }

        // reads every shown folder again.
        void refresh() {
            listings_.clear();
            rebuild();
        }

        ftxui::Element OnRender() override {
            if (signature() != signature_) {
                rebuild();  // a command opened or closed folders
            }
            if (Focused()) {
                chosen_ = true;
            }
            class Canvas : public ftxui::Node {
            public:
                Canvas(ExplorerView* view, bool focused) : view_(view), focused_(focused) {}
                void ComputeRequirement() override {
                    requirement_.min_x = 1;
                    requirement_.min_y = 1;
                    requirement_.flex_grow_x = requirement_.flex_grow_y = 1;
                    requirement_.flex_shrink_x = requirement_.flex_shrink_y = 1;
                }
                void Render(ftxui::Screen& screen) override {
                    view_->paint(screen, ftxui::Box::Intersection(box_, screen.stencil), focused_);
                }

            private:
                ExplorerView* view_;
                bool focused_;
            };
            return std::make_shared<Canvas>(this, Focused());
        }

        void paint(ftxui::Screen& screen, const ftxui::Box& box, bool focused) {
            box_ = box;
            if (box.x_max < box.x_min || box.y_max < box.y_min) {
                return;
            }
            const int view_rows = box.y_max - box.y_min + 1;
            scroll_.view_rows = view_rows;
            scroll_.content_rows = std::max(static_cast<int>(rows_.size()), view_rows);
            scroll_.top = std::clamp(scroll_.top, 0, scroll_.content_rows - view_rows);
            if (follow_) {
                scroll_.keep_visible(static_cast<int>(selected_));
                follow_ = false;
            }

            const Theme& theme = current_theme();
            const std::string& open_path = active_path();
            // where a new file would go: marked with a '+', so Ctrl+N is no surprise.
            const std::string new_files = current_new_file_folder().lexically_normal().string();
            const int bar_x = box.x_max;        // the scroll bar
            const int marks_x = bar_x - 2;      // Git's mark: " M"
            const ftxui::Color guide = ftxui::Color::RGB(80, 80, 80);
            const ftxui::Color guide_lit = ftxui::Color::RGB(150, 150, 150);

            for (int i = 0; i < view_rows; ++i) {
                const std::size_t index = static_cast<std::size_t>(scroll_.top + i);
                if (index >= rows_.size()) {
                    break;
                }
                const Row& row = rows_[index];
                const int y = box.y_min + i;
                const bool selected = index == selected_;
                const bool lit = selected && focused;

                // the margin: a bar on the row the keys are on.
                int x = box.x_min;
                x = put(screen, x, y, marks_x, lit ? "┃" : " ", theme.accent, lit, false);
                // a line for every folder the row is inside of.
                for (int d = 0; d < row.depth; ++d) {
                    const bool under_selected = focused && selected_ < rows_.size() && rows_[selected_].folder &&
                                                rows_[selected_].depth == d && row.path.compare(0, rows_[selected_].path.size() + 1, rows_[selected_].path + "/") == 0;
                    x = put(screen, x, y, marks_x, "│", under_selected ? guide_lit : guide, false, false);
                }

                const Document* document = row.folder ? nullptr : workspace.find(row.path);
                const bool unsaved = row.folder ? workspace.unsaved_under(row.path) : document && document->unsaved();
                const bool ignored = git_status.ignored(row.path);
                ftxui::Color color = ftxui::Color::Default;
                if (!row.folder && row.path == open_path) {
                    color = theme.open_file;  // the file in the active window
                } else if (unsaved) {
                    color = ftxui::Color::Yellow1;
                }
                const bool dim = color == ftxui::Color::Default && ignored;
                const bool takes_new_files = row.folder && fs::path(row.path).lexically_normal().string() == new_files;

                const std::string prefix = row.folder ? (workspace.opened_folders.count(row.path) != 0 ? "v " : "> ") : "  ";
                // cells after the name: '*' for unsaved, the new-files mark.
                const int suffix_cells = (!row.folder && unsaved ? 1 : 0) + (takes_new_files ? new_files_mark_cells : 0);
                x = put(screen, x, y, marks_x, prefix, color, lit, dim);
                const int room = marks_x - x - suffix_cells;
                x = put(screen, x, y, marks_x, truncate_name(row.name, room), color, lit, dim);
                if (!row.folder && unsaved) {
                    x = put(screen, x, y, marks_x, "*", color, lit, dim);
                }
                if (takes_new_files) {
                    put(screen, x, y, marks_x, new_files_mark, theme.accent, true, false);
                }
                paint_git_mark(screen, marks_x, y, row);
            }
            paint_scroll_bar(screen, bar_x, box.y_min, box.y_max, scroll_.top, static_cast<int>(rows_.size()));
        }

        bool OnEvent(Event event) override {
            if (event.is_mouse()) {
                return on_mouse(event);
            }
            if (!Focused() || rows_.empty()) {
                return false;
            }
            chosen_ = true;
            const int last = static_cast<int>(rows_.size()) - 1;
            const int at = static_cast<int>(selected_);
            const int page = std::max(1, scroll_.view_rows - 1);
            const Row& row = rows_[selected_];

            // every key that moves stays in the explorer: at the first or last
            // row it simply doesn't move (it used to jump to the menu bar).
            if (event == Event::ArrowUp || event == Event::Character('k')) { select(std::max(0, at - 1)); return true; }
            if (event == Event::ArrowDown || event == Event::Character('j')) { select(std::min(last, at + 1)); return true; }
            if (event == Event::Tab) { select(at == last ? 0 : at + 1); return true; }
            if (event == Event::TabReverse) { select(at == 0 ? last : at - 1); return true; }
            if (event == Event::PageUp) { select(std::max(0, at - page)); return true; }
            if (event == Event::PageDown) { select(std::min(last, at + page)); return true; }
            if (event == Event::Home) { select(0); return true; }
            if (event == Event::End) { select(last); return true; }

            // Right opens a folder, then goes into it; Left closes it, or goes
            // to the folder the row is in (like vscode).
            if (event == Event::ArrowRight || event == Event::Character('l')) {
                if (row.folder && workspace.opened_folders.count(row.path) == 0) {
                    toggle(row.path);
                } else if (row.folder && at < last && rows_[selected_ + 1].depth > row.depth) {
                    select(at + 1);
                }
                return true;
            }
            if (event == Event::ArrowLeft || event == Event::Character('h')) {
                if (row.folder && workspace.opened_folders.count(row.path) != 0) {
                    toggle(row.path);
                } else {
                    for (int i = at - 1; i >= 0; --i) {
                        if (rows_[static_cast<std::size_t>(i)].depth < row.depth) {
                            select(i);
                            break;
                        }
                    }
                }
                return true;
            }
            if (event == Event::Return) {
                activate(row);
                return true;
            }
            // F2 renames and Delete deletes the row, on the command line where
            // it can still be changed or cancelled.
            if (event == Event::F2 && open_command_line) {
                open_command_line("rename " + row.name);
                return true;
            }
            if (event == Event::Delete && open_command_line) {
                open_command_line("rm");
                return true;
            }
            return false;
        }

        ftxui::Box panel_box;

    private:
        // changes when a folder is opened or closed by anything.
        std::size_t signature() const {
            std::size_t sum = workspace.opened_folders.size();
            for (const std::string& folder : workspace.opened_folders) {
                sum += std::hash<std::string>{}(folder);
            }
            return sum;
        }

        const std::vector<DirEntry>& listing(const std::string& folder) {
            const auto found = listings_.find(folder);
            if (found != listings_.end()) {
                return found->second;
            }
            // a folder that can't be read shows its error instead of crashing.
            std::string error;
            std::vector<DirEntry> entries = list_directory_entries(folder, error);
            if (!error.empty()) {
                command_error = error;
            }
            return listings_[folder] = std::move(entries);
        }

        void add_rows(const std::string& folder, int depth) {
            if (depth > 64) {
                return;  // links that lead back into themselves
            }
            const std::vector<DirEntry> entries = listing(folder);
            for (const DirEntry& entry : entries) {
                // .git: never something you want to browse in an editor.
                if (!entry.is_directory || entry.path.filename() == ".git") {
                    continue;
                }
                rows_.push_back({entry.path.string(), entry.path.filename().string(), depth, true});
                if (workspace.opened_folders.count(entry.path.string()) != 0) {
                    add_rows(entry.path.string(), depth + 1);
                }
            }
            for (const DirEntry& entry : entries) {
                if (entry.is_regular_file) {
                    rows_.push_back({entry.path.string(), entry.path.filename().string(), depth, false});
                }
            }
        }

        void rebuild() {
            const std::string keep = selected_ < rows_.size() ? rows_[selected_].path : std::string();
            rows_.clear();
            add_rows(root_.string(), 0);
            signature_ = signature();
            const auto same = std::find_if(rows_.begin(), rows_.end(), [&](const Row& row) { return row.path == keep; });
            if (same != rows_.end()) {
                selected_ = static_cast<std::size_t>(same - rows_.begin());
            } else {
                selected_ = rows_.empty() ? 0 : std::min(selected_, rows_.size() - 1);
            }
        }

        void select(int index) {
            selected_ = static_cast<std::size_t>(index);
            follow_ = true;
        }

        void toggle(const std::string& folder) {
            if (workspace.opened_folders.erase(folder) == 0) {
                workspace.opened_folders.insert(folder);
                listings_.erase(folder);  // read when it opens: what is in it may have changed
            }
            rebuild();
            call(hooks.refresh_explorer);  // the folders to watch changed
        }

        void activate(const Row& row) {
            if (row.folder) {
                toggle(row.path);
                return;
            }
            std::string error;
            if (!workspace.open(row.path, error)) {
                command_error = error;  // an unreadable file must never look empty
                return;
            }
            call(hooks.show_editor);
        }

        bool on_mouse(Event event) {
            const ftxui::Mouse& mouse = event.mouse();
            if (mouse.button != ftxui::Mouse::Left || mouse.motion != ftxui::Mouse::Pressed || !box_.Contain(mouse.x, mouse.y)) {
                return false;
            }
            TakeFocus();
            chosen_ = true;
            const std::size_t index = static_cast<std::size_t>(scroll_.top + mouse.y - box_.y_min);
            if (index < rows_.size()) {
                selected_ = index;
                activate(Row(rows_[index]));
            }
            return true;
        }

        void paint_git_mark(ftxui::Screen& screen, int x, int y, const Row& row) {
            if (!git_status.in_repository) {
                return;
            }
            const char state = git_status.state_of(row.path);
            const char* mark = "";
            ftxui::Color color = ftxui::Color::Default;
            if (row.folder) {
                if (state == '?') { mark = "U"; color = ftxui::Color::GreenLight; }
                else if (git_status.changes_under(row.path)) { mark = "•"; color = ftxui::Color::Gold1; }
            } else {
                switch (state) {
                    case 'M': mark = "M"; color = ftxui::Color::Gold1; break;
                    case 'A': mark = "A"; color = ftxui::Color::GreenLight; break;
                    case '?': mark = "U"; color = ftxui::Color::GreenLight; break;
                    case 'D': mark = "D"; color = ftxui::Color::RedLight; break;
                    case 'R': mark = "R"; color = ftxui::Color::SkyBlue1; break;
                    case 'U': mark = "!"; color = ftxui::Color::Red; break;
                    default: break;
                }
            }
            if (*mark) {
                put(screen, x + 1, y, x + 2, mark, color, false, false);
            }
        }

    public:
        ScrollState scroll_;

    private:
        fs::path root_;
        std::unordered_map<std::string, std::vector<DirEntry>> listings_;
        std::vector<Row> rows_;
        std::size_t signature_ = 0;
        std::size_t selected_ = 0;
        bool chosen_ = false;  // the user selected a row (it names the active folder)
        bool follow_ = false;  // scroll to the selected row on the next frame
        ftxui::Box box_;
    };

}  // namespace

ExplorerPanel make_explorer_panel(const fs::path& root, int& width) {
    auto view = std::make_shared<ExplorerView>(root);

    ExplorerPanel explorer;
    explorer.rows = view;
    explorer.refresh = [view] { view->refresh(); };
    explorer.contains = [view](int x, int y) { return view->contains(x, y); };
    explorer.selected_path = [view] { return view->selected_path(); };
    explorer.selected_is_folder = [view] { return view->selected_is_folder(); };
    explorer.shown_folders = [view] { return view->shown_folders(); };

    auto panel = ftxui::Renderer(view, [view, &width] {
        // the mark on the title when new files go to the opened folder itself.
        const bool top_takes_new = current_new_file_folder().lexically_normal() == fs::path(workspace.root).lexically_normal();
        return ftxui::border(ftxui::vbox({
                   ftxui::hbox({ftxui::text(" "), ftxui::bold(ftxui::text("Explorer")),
                                top_takes_new ? ftxui::bold(ftxui::color(current_theme().accent, ftxui::text(new_files_mark))) : ftxui::text("")}),
                   ftxui::separator(),
                   view->Render() | ftxui::flex,
               })) |
               ftxui::reflect(view->panel_box) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width);
    });

    // the wheel over the explorer scrolls its view by whole rows, and never
    // moves the selection or the focus.
    explorer.panel = ftxui::CatchEvent(panel, [view](ftxui::Event event) {
        if (!event.is_mouse()) {
            return false;
        }
        const ftxui::Mouse& mouse = event.mouse();
        if (mouse.button != ftxui::Mouse::WheelUp && mouse.button != ftxui::Mouse::WheelDown) {
            return false;
        }
        if (!view->contains(mouse.x, mouse.y)) {
            return false;  // over the editor: not for the explorer
        }
        const int rows_per_notch = get_mouse_sensitivity();
        view->scroll_.scroll_by(mouse.button == ftxui::Mouse::WheelUp ? -rows_per_notch : rows_per_notch);
        call(hooks.redraw);
        return true;
    });
    return explorer;
}
