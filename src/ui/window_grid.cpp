#include "ui/window_grid.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

#include "core/version.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "syntax/theme.hpp"
#include "ui/editor_view.hpp"
#include "ui/widgets.hpp"

namespace {

    // a line between two windows.
    class LineNode : public ftxui::Node {
    public:
        explicit LineNode(bool upright) : upright_(upright) {}
        void ComputeRequirement() override {
            requirement_.min_x = 1;
            requirement_.min_y = 1;
        }
        void Render(ftxui::Screen& screen) override {
            for (int y = box_.y_min; y <= box_.y_max; ++y) {
                for (int x = box_.x_min; x <= box_.x_max; ++x) {
                    ftxui::Cell& cell = screen.CellAt(x, y);
                    cell.character = upright_ ? "│" : "─";
                    cell.dim = true;
                }
            }
        }

    private:
        bool upright_;
    };

    // two elements side by side (or one above the other) with a line between
    // them; the first takes `ratio` of the room.
    class SplitNode : public ftxui::Node {
    public:
        SplitNode(ftxui::Element first, ftxui::Element second, bool stacked, float ratio, ftxui::Box& first_box,
                  ftxui::Box& second_box, ftxui::Box& separator_box)
            : ftxui::Node({std::move(first), std::make_shared<LineNode>(!stacked), std::move(second)}),
              stacked_(stacked), ratio_(ratio), first_box_(first_box), second_box_(second_box), separator_box_(separator_box) {}

        void ComputeRequirement() override {
            ftxui::Node::ComputeRequirement();
            requirement_.min_x = 3;
            requirement_.min_y = 3;
            requirement_.flex_grow_x = requirement_.flex_grow_y = 1;
            requirement_.flex_shrink_x = requirement_.flex_shrink_y = 1;
            // the cursor of the focused window
            for (const auto& child : children_) {
                if (requirement_.focused.Prefer(child->requirement().focused)) {
                    requirement_.focused = child->requirement().focused;
                }
            }
        }

        void SetBox(ftxui::Box box) override {
            ftxui::Node::SetBox(box);
            const int total = stacked_ ? box.y_max - box.y_min + 1 : box.x_max - box.x_min + 1;
            const int room = std::max(0, total - 1);
            const int least = std::min(stacked_ ? 2 : 8, room / 2);
            const int first = std::clamp(static_cast<int>(std::lround(static_cast<float>(room) * ratio_)), least, room - least);

            ftxui::Box a = box, line = box, b = box;
            if (stacked_) {
                a.y_max = box.y_min + first - 1;
                line.y_min = line.y_max = box.y_min + first;
                b.y_min = box.y_min + first + 1;
            } else {
                a.x_max = box.x_min + first - 1;
                line.x_min = line.x_max = box.x_min + first;
                b.x_min = box.x_min + first + 1;
            }
            first_box_ = a;
            second_box_ = b;
            separator_box_ = line;
            children_[0]->SetBox(a);
            children_[1]->SetBox(line);
            children_[2]->SetBox(b);
        }

    private:
        bool stacked_;
        float ratio_;
        ftxui::Box& first_box_;
        ftxui::Box& second_box_;
        ftxui::Box& separator_box_;
    };

    // the name on a window's title row: the file name, its folder in front
    // when another open file has the same name.
    std::string title_of(const Document& document) {
        int same = 0;
        for (const auto& other : workspace.documents()) {
            same += other->name() == document.name() ? 1 : 0;
        }
        const std::string folder = std::filesystem::path(document.path()).parent_path().filename().string();
        return same > 1 && !folder.empty() && folder != "." ? folder + "/" + document.name() : document.name();
    }

}  // namespace

// one window: its title row (when there are several) and what it shows: the
// text, the warning for a binary file, or a greeting.
class WindowView : public ftxui::ComponentBase {
public:
    explicit WindowView(int window) : window_(window), editor_(make_editor_view(window)) {
        open_anyway_ = ftxui::Button("Open Anyway", [window] {
            if (Document* document = workspace.document_of(window)) {
                document->open_anyway();
            }
        }, create_plain_button_option());
        Add(editor_);
        Add(open_anyway_);
    }

    EditorView& editor() { return *editor_; }
    bool contains(int x, int y) const { return box_.Contain(x, y); }
    bool Focusable() const override { return true; }

    ftxui::Component ActiveChild() override {
        const Document* document = workspace.document_of(window_);
        return document && document->hidden_as_binary() ? open_anyway_ : ftxui::Component(editor_);
    }

    ftxui::Element OnRender() override {
        const Document* document = workspace.document_of(window_);
        const bool several = workspace.window_count() > 1;
        ftxui::Element page;
        if (!document) {
            page = several ? ftxui::vcenter(ftxui::dim(ftxui::paragraphAlignCenter("No file here: pick one in the explorer, or >e name")))
                           : ftxui::vcenter(ftxui::vbox({
                                 ftxui::hcenter(ftxui::bold(ftxui::text("Welcome to Itahi Text Editor" + ITAHI_VERSION))),
                                 ftxui::separatorEmpty(),
                                 ftxui::dim(ftxui::paragraphAlignCenter("Select a file in the Explorer panel on the left to start editing.")),
                                 ftxui::separatorEmpty(),
                                 ftxui::dim(ftxui::paragraphAlignCenter("Ctrl+N new file   >  commands   F1  keys and help")),
                             }));
        } else if (document->hidden_as_binary()) {
            page = ftxui::vcenter(ftxui::vbox({
                ftxui::hcenter(ftxui::bold(ftxui::color(ftxui::Color::Yellow, ftxui::text("Warning")))),
                ftxui::separatorEmpty(),
                ftxui::paragraphAlignCenter("The file is not displayed in the text editor because it is either binary or uses an unsupported text encoding."),
                ftxui::dim(ftxui::paragraphAlignCenter("Opening it anyway could break the Itahi Text Editor.")),
                ftxui::separatorEmpty(),
                ftxui::hcenter(open_anyway_->Render()),
            }));
        } else {
            page = editor_->Render();
        }
        if (!several) {
            return ftxui::flex(page) | ftxui::reflect(box_);
        }

        // the title row: the window's number and file; the active window's
        // stands out, so it is always clear where the keys go.
        const bool active = workspace.active_window() == window_;
        const Theme& theme = current_theme();
        ftxui::Element name = ftxui::text(document ? title_of(*document) + (document->unsaved() ? "*" : "") : "(no file)");
        if (document && document->unsaved()) {
            name = ftxui::color(ftxui::Color::Yellow1, name);
        }
        ftxui::Element title = ftxui::hbox({
            ftxui::text(" " + std::to_string(workspace.window_number(window_)) + "  "),
            name,
            ftxui::filler(),
            ftxui::text(" x ") | ftxui::reflect(close_box_),
        }) | ftxui::reflect(title_box_);
        title = active ? ftxui::bold(title) | ftxui::bgcolor(theme.status_background) | ftxui::color(theme.status_foreground)
                       : ftxui::dim(title);
        return ftxui::vbox({title, ftxui::flex(page)}) | ftxui::reflect(box_);
    }

    bool OnEvent(ftxui::Event event) override {
        if (event.is_mouse()) {
            const ftxui::Mouse& mouse = event.mouse();
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed && box_.Contain(mouse.x, mouse.y)) {
                workspace.focus_window(window_);
                TakeFocus();
                if (workspace.window_count() > 1 && close_box_.Contain(mouse.x, mouse.y)) {
                    std::string error;
                    workspace.close_window(window_, error);
                    return true;
                }
                if (workspace.window_count() > 1 && title_box_.Contain(mouse.x, mouse.y)) {
                    return true;
                }
            }
        }
        return ActiveChild()->OnEvent(event);
    }

private:
    int window_;
    std::shared_ptr<EditorView> editor_;
    ftxui::Component open_anyway_;
    ftxui::Box box_;
    ftxui::Box title_box_;
    ftxui::Box close_box_;
};

std::shared_ptr<WindowGrid> make_window_grid() {
    return std::make_shared<WindowGrid>();
}

void WindowGrid::sync() {
    for (const auto& column : workspace.columns()) {
        for (const int id : column) {
            if (views_.count(id) == 0) {
                auto view = std::make_shared<WindowView>(id);
                Add(view);
                views_[id] = std::move(view);
            }
        }
    }
    for (auto it = views_.begin(); it != views_.end();) {
        if (!workspace.has_window(it->first)) {
            it->second->Detach();
            it = views_.erase(it);
        } else {
            ++it;
        }
    }
}

ftxui::Component WindowGrid::ActiveChild() {
    sync();
    const auto found = views_.find(workspace.active_window());
    return found == views_.end() ? nullptr : ftxui::Component(found->second);
}

void WindowGrid::SetActiveChild(ftxui::ComponentBase* child) {
    for (const auto& [id, view] : views_) {
        if (view.get() == child) {
            workspace.focus_window(id);
            return;
        }
    }
}

EditorView& WindowGrid::active_editor() {
    sync();
    return views_.at(workspace.active_window())->editor();
}

ftxui::Element WindowGrid::OnRender() {
    sync();
    const auto& columns = workspace.columns();
    ftxui::Elements drawn;
    for (std::size_t c = 0; c < columns.size() && c < 2; ++c) {
        if (columns[c].size() == 1) {
            drawn.push_back(views_.at(columns[c][0])->Render());
        } else {
            static ftxui::Box top_box, bottom_box;
            drawn.push_back(std::make_shared<SplitNode>(views_.at(columns[c][0])->Render(), views_.at(columns[c][1])->Render(),
                                                        true, row_ratio_[c], top_box, bottom_box, row_separator_[c]));
        }
    }
    ftxui::Element all = drawn.size() == 1
        ? (drawn[0] | ftxui::reflect(column_box_[0]))
        : ftxui::Element(std::make_shared<SplitNode>(drawn[0], drawn[1], false, column_ratio_, column_box_[0], column_box_[1], column_separator_));
    return ftxui::flex(all) | ftxui::reflect(box_);
}

bool WindowGrid::OnEvent(ftxui::Event event) {
    sync();
    if (event.is_mouse()) {
        const ftxui::Mouse& mouse = event.mouse();
        const auto& columns = workspace.columns();
        if (dragging_ >= 0) {
            if (mouse.motion == ftxui::Mouse::Released) {
                dragging_ = -1;
                captured_mouse_.reset();
            } else if (dragging_ == 0) {
                const int width = box_.x_max - box_.x_min;
                column_ratio_ = width > 0 ? std::clamp(static_cast<float>(mouse.x - box_.x_min) / static_cast<float>(width), 0.15f, 0.85f) : 0.5f;
            } else {
                const ftxui::Box& column = column_box_[dragging_ - 1];
                const int height = column.y_max - column.y_min;
                row_ratio_[dragging_ - 1] = height > 0 ? std::clamp(static_cast<float>(mouse.y - column.y_min) / static_cast<float>(height), 0.15f, 0.85f) : 0.5f;
            }
            return true;
        }
        if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            int line = -1;
            if (columns.size() > 1 && column_separator_.Contain(mouse.x, mouse.y)) {
                line = 0;
            }
            for (std::size_t c = 0; c < columns.size() && c < 2; ++c) {
                if (columns[c].size() > 1 && row_separator_[c].Contain(mouse.x, mouse.y)) {
                    line = static_cast<int>(c) + 1;
                }
            }
            if (line >= 0) {
                dragging_ = line;
                captured_mouse_ = CaptureMouse(event);
                return true;
            }
        }
        // every window sees the mouse: the one under the pointer takes a
        // click, the one a selection is dragged in follows the pointer.
        bool handled = false;
        const auto views = views_;  // a click can close a window
        for (const auto& [id, view] : views) {
            handled = view->OnEvent(event) || handled;
        }
        return handled;
    }
    if (!Focused()) {
        return false;
    }
    const ftxui::Component active = ActiveChild();
    return active && active->OnEvent(event);
}

void WindowGrid::wheel(int x, int y, int rows) {
    sync();
    for (const auto& [id, view] : views_) {
        if (view->contains(x, y)) {
            view->editor().wheel(rows);
            return;
        }
    }
    active_editor().wheel(rows);
}

void WindowGrid::equalize() {
    column_ratio_ = row_ratio_[0] = row_ratio_[1] = 0.5f;
}

void WindowGrid::resize_active(int columns, int rows) {
    const auto& layout = workspace.columns();
    for (std::size_t c = 0; c < layout.size() && c < 2; ++c) {
        for (std::size_t r = 0; r < layout[c].size(); ++r) {
            if (layout[c][r] != workspace.active_window()) {
                continue;
            }
            const int width = std::max(1, box_.x_max - box_.x_min);
            const int height = std::max(1, column_box_[c].y_max - column_box_[c].y_min);
            // the active window grows: the line moves away from it.
            column_ratio_ = std::clamp(column_ratio_ + (c == 0 ? 1.f : -1.f) * static_cast<float>(columns) / static_cast<float>(width), 0.15f, 0.85f);
            row_ratio_[c] = std::clamp(row_ratio_[c] + (r == 0 ? 1.f : -1.f) * static_cast<float>(rows) / static_cast<float>(height), 0.15f, 0.85f);
        }
    }
}
