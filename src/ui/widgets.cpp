
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include "ui/widgets.hpp"

#include "editor/settings.hpp"
#include "editor/state.hpp"

namespace {

    // decorator recording the exact box given by the parent during layout. Unlike
    // `ftxui::reflect`, Render() does not intersect the recorded box with the
    // screen stencil, so off-screen / larger-than-viewport content keeps a valid
    // box.
    class BoxCapture : public ftxui::Node {
    public:
    BoxCapture(ftxui::Element child, ftxui::Box& box) : ftxui::Node({std::move(child)}), captured_box_(box) {}

    void ComputeRequirement() override {
        ftxui::Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
    }

    void SetBox(ftxui::Box box) override {
        captured_box_ = box;
        ftxui::Node::SetBox(box);
        children_[0]->SetBox(box);
    }

    private:
    ftxui::Box& captured_box_;
    };

    // single line inputs: Up/Down are not given to the Input (it would use
    // them to jump to the start/end of the text) so the parent container moves
    // to the previous/next row in one key press.
    class PassVerticalArrows : public ftxui::ComponentBase {
    public:
    explicit PassVerticalArrows(ftxui::Component child) { Add(std::move(child)); }

    bool OnEvent(ftxui::Event event) override {
        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
            return false;
        }
        return ftxui::ComponentBase::OnEvent(event);
    }
    };

    // ftxui::vscroll_indicator only replaces the character of the last column,
    // so the bar keeps the colors of whatever was drawn there (orange for the
    // open file, yellow for unsaved ones, syntax colors in the editor). This
    // copy draws it on its own: always white, no bold / dim / underline.
    class WhiteScrollIndicator : public ftxui::Node {
    public:
    explicit WhiteScrollIndicator(ftxui::Element child) : ftxui::Node({std::move(child)}) {}

    void ComputeRequirement() override {
        ftxui::Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
        requirement_.min_x++;
    }

    void SetBox(ftxui::Box box) override {
        box_ = box;
        box.x_max--;
        children_[0]->SetBox(box);
    }

    void Render(ftxui::Screen& screen) override {
        ftxui::Node::Render(screen);
        const ftxui::Box& stencil = screen.stencil;
        paint_scroll_bar(screen, stencil.x_max, stencil.y_min, stencil.y_max, stencil.y_min - box_.y_min,
                         box_.y_max - box_.y_min + 1);
    }
    };

    // a vertical viewport with an explicit top row (see scroll_frame()).
    class ScrollFrame : public ftxui::Node {
    public:
    ScrollFrame(ftxui::Element child, ScrollState& state) : ftxui::Node({std::move(child)}), state_(state) {}

    void ComputeRequirement() override {
        ftxui::Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
    }

    void SetBox(ftxui::Box box) override {
        ftxui::Node::SetBox(box);
        state_.view_rows = std::max(1, box.y_max - box.y_min + 1);
        state_.content_rows = std::max(children_[0]->requirement().min_y, state_.view_rows);
        state_.top = std::clamp(state_.top, 0, state_.content_rows - state_.view_rows);

        ftxui::Box child_box = box;
        child_box.y_min = box.y_min - state_.top;
        child_box.y_max = child_box.y_min + state_.content_rows - 1;
        children_[0]->SetBox(child_box);
    }

    void Render(ftxui::Screen& screen) override {
        const ftxui::Box saved = screen.stencil;
        screen.stencil = ftxui::Box::Intersection(box_, screen.stencil);
        children_[0]->Render(screen);
        screen.stencil = saved;
    }

    private:
    ScrollState& state_;
    };

} // namespace

void ScrollState::scroll_by(int rows) {
    top = std::clamp(top + rows, 0, std::max(0, content_rows - view_rows));
}

void ScrollState::keep_visible(int row) {
    if (row < top) {
        top = row;
    } else if (row >= top + view_rows) {
        top = row - view_rows + 1;
    }
}

ftxui::Element scroll_frame(ftxui::Element child, ScrollState& state) {
    return std::make_shared<ScrollFrame>(std::move(child), state);
}

void paint_scroll_bar(ftxui::Screen& screen, int x, int y_min, int y_max, int top, int total_rows) {
    for (int y = y_min; y <= y_max; ++y) {
        ftxui::Cell& cell = screen.CellAt(x, y);
        cell.character = " ";
        cell.foreground_color = ftxui::Color::White;
        cell.background_color = ftxui::Color::Default;
        cell.bold = cell.dim = cell.italic = cell.underlined = cell.inverted = false;
    }
    const int size_outer = y_max - y_min + 1;
    if (total_rows <= 0 || size_outer >= total_rows) {
        return;  // everything fits: no bar
    }

    // in half rows: the bar can start and end in the middle of a cell.
    const int size = std::max(2 * size_outer * size_outer / total_rows, 1);
    const int start_y = 2 * y_min + 2 * top * size_outer / total_rows;
    for (int y = y_min; y <= y_max; ++y) {
        const bool up = start_y <= 2 * y && 2 * y <= start_y + size;
        const bool down = start_y <= 2 * y + 1 && 2 * y + 1 <= start_y + size;
        screen.CellAt(x, y).character = up ? (down ? "┃" : "╹") : (down ? "╻" : " ");
    }
}

void mark_active_row_cell(ftxui::Screen& screen, int x, int y, int x_min, int mark, ftxui::Color shade) {
    ftxui::Cell& cell = screen.CellAt(x, y);
    if (mark == ROW_MARK_SHADED) {
        if (cell.background_color == ftxui::Color::Default) {
            cell.background_color = shade;
        }
        return;
    }
    cell.underlined = true;
    // a dim gray line (256-color 242), so it doesn't outshine the text; or,
    // for ROW_MARK_TEXT_COLOR, the underline color reset (SGR 59), since a
    // gray one set earlier (another cell, the last frame) would still be on.
    // the terminal draws an underline in the text's color, so on a
    // highlighted line it changes color with every word. Terminals also have
    // a separate underline color (SGR 58), which FTXUI doesn't know about:
    // it is sent in front of the cell's character instead. FTXUI never resets
    // all attributes at once, so it stays set. Full-width characters are
    // skipped (FTXUI measures them to know which cell to skip next); they keep
    // the color set by the cell before them.
    const bool second_half = cell.character.empty() && x > x_min &&
                             ftxui::string_width(screen.CellAt(x - 1, y).character) == 2;
    if (second_half || ftxui::string_width(cell.character) == 2) {
        return;
    }
    const char* const underline_color = mark == ROW_MARK_TEXT_COLOR ? "\x1B[59m" : "\x1B[58;5;242m";
    cell.character = underline_color + (cell.character.empty() ? std::string(" ") : cell.character);
}

ftxui::Element white_vscroll_indicator(ftxui::Element child) {
    return std::make_shared<WhiteScrollIndicator>(std::move(child));
}

ftxui::Decorator capture_box(ftxui::Box& box) {
    return [&box](ftxui::Element child) -> ftxui::Element {
        return std::make_shared<BoxCapture>(std::move(child), box);
    };
}


// numeric input custom
ftxui::Component make_numeric_input_row(std::string& target_var, const std::string& label_text, int max_digits_taken, int max_width) {
    ftxui::Component input_num = ftxui::Input(&target_var, "");

    // the label without its trailing ": " names the setting in the errors.
    const std::string setting_name = label_text.substr(0, label_text.find_last_not_of(": ") + 1);

    ftxui::ComponentBase* input_ptr = input_num.get();

    input_num |= ftxui::CatchEvent([&target_var, max_digits_taken, setting_name, input_ptr](ftxui::Event event) {
        // terminals that send ^H for Backspace (see the FTXUI patch).
        if (event == ftxui::Event::CtrlH) {
            return input_ptr->OnEvent(ftxui::Event::Backspace);
        }

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
                command_error = setting_name + " only accepts numbers";
                return true;
            }
            // Stop accepting text inputs if the max_digits is reached
            if (target_var.size() >= static_cast<std::size_t>(max_digits_taken)) {
                command_error = setting_name + " accepts at most " + std::to_string(max_digits_taken) + " digits";
                return true;
            }
        }
        return false;
    });

    input_num = std::make_shared<PassVerticalArrows>(input_num);

    return ftxui::Renderer(input_num, [input_num, label_text, max_width]() {
        return ftxui::hbox({
            menu_label(label_text),
            ftxui::text("[ "),
            ftxui::underlined(
                ftxui::size(ftxui::WIDTH, ftxui::EQUAL, max_width)(input_num->Render())
            ),
            ftxui::text(" ]"),
        });
    });
}

ftxui::Element menu_label(const std::string& label) {
    // the labels of a page end in the same column, so the controls line up.
    constexpr int label_columns = 24;
    return ftxui::text(label) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, label_columns);
}

ftxui::Component menu_row(ftxui::Component inner, std::string hint) {
    return ftxui::Renderer(inner, [inner, hint = std::move(hint)] {
        const bool focused = inner->Focused();
        // the margin on the left holds a bar while the row has the focus.
        ftxui::Element bar = focused ? ftxui::bold(ftxui::text(" ┃ ")) : ftxui::text("   ");
        ftxui::Element body = inner->Render();
        if (!hint.empty()) {
            constexpr int hint_column = 24;
            body = ftxui::hbox({body | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, hint_column), ftxui::dim(ftxui::text("  " + hint))});
        }
        ftxui::Element row = ftxui::hbox({bar, body, ftxui::text("  ")});
        return focused ? row | ftxui::focus : row;
    });
}

ftxui::Component menu_title(std::string title) {
    return ftxui::Renderer([title = std::move(title)] {
        return ftxui::vbox({ftxui::text(""), ftxui::hbox({ftxui::text("   "), ftxui::bold(ftxui::text(title))}), ftxui::text("")});
    });
}

ftxui::Component menu_gap() {
    return ftxui::Renderer([] { return ftxui::text(""); });
}

namespace {

    // a page of menu rows with its own scrolling. The mouse wheel moves the
    // view wherever the focus is (FTXUI's own frame only follows the focused
    // row, and its containers ignore keys while they don't have the focus, so
    // a page reached with a click on the menu bar couldn't be scrolled);
    // moving through the rows with the keyboard scrolls just enough to keep
    // the focused row in view.
    class MenuPage : public ftxui::ComponentBase {
    public:
        explicit MenuPage(ftxui::Components rows) : rows_(ftxui::Container::Vertical(std::move(rows))) {
            Add(rows_);
            row_boxes_.resize(rows_->ChildCount());
        }

        ftxui::Element OnRender() override {
            ftxui::Elements drawn;
            int focused = -1;
            for (std::size_t i = 0; i < rows_->ChildCount(); ++i) {
                ftxui::Component& row = rows_->ChildAt(i);
                drawn.push_back(row->Render() | capture_box(row_boxes_[i]));
                if (row->Focused()) {
                    focused = static_cast<int>(i);
                }
            }
            drawn.push_back(ftxui::text(""));

            // the row with the focus changed (or grew: an opened dropdown):
            // scroll to it. The boxes are the last frame's, so one more frame
            // is asked for when the view had to move.
            const ftxui::Box focused_box = focused >= 0 ? row_boxes_[static_cast<std::size_t>(focused)] : ftxui::Box{};
            const int height = focused_box.y_max - focused_box.y_min;
            if (focused >= 0 && (focused != followed_row_ || height != followed_height_ || follow_again_)) {
                const int before = scroll_.top;
                scroll_.keep_visible(focused_box.y_max - content_box_.y_min);
                scroll_.keep_visible(focused_box.y_min - content_box_.y_min);
                // the title above the first rows comes back into view with them.
                if (focused_box.y_min - content_box_.y_min <= 3) {
                    scroll_.top = 0;
                }
                follow_again_ = focused != followed_row_ && !follow_again_;
                followed_row_ = focused;
                followed_height_ = height;
                if ((scroll_.top != before || follow_again_) && hooks.redraw) {
                    hooks.redraw();
                }
            } else if (focused < 0) {
                followed_row_ = -1;
            }

            return scroll_frame(white_vscroll_indicator(ftxui::vbox(std::move(drawn)) | capture_box(content_box_)), scroll_) |
                   ftxui::flex | ftxui::reflect(box_);
        }

        bool OnEvent(ftxui::Event event) override {
            if (event.is_mouse()) {
                const ftxui::Mouse& mouse = event.mouse();
                const bool wheel = mouse.button == ftxui::Mouse::WheelUp || mouse.button == ftxui::Mouse::WheelDown;
                if (wheel && box_.Contain(mouse.x, mouse.y)) {
                    // (not handed to the rows: their container would move the
                    // focus with the wheel instead.)
                    const int step = get_mouse_sensitivity();
                    scroll_.scroll_by(mouse.button == ftxui::Mouse::WheelUp ? -step : step);
                    return true;
                }
                // rows scrolled out of the page take no clicks.
                if (!wheel && mouse.motion == ftxui::Mouse::Pressed && !box_.Contain(mouse.x, mouse.y)) {
                    return false;
                }
            }
            return rows_->OnEvent(event);
        }

    private:
        ftxui::Component rows_;
        std::vector<ftxui::Box> row_boxes_;
        ftxui::Box content_box_;
        ftxui::Box box_;
        ScrollState scroll_;
        int followed_row_ = -1;
        int followed_height_ = 0;
        bool follow_again_ = false;
    };

}  // namespace

ftxui::Component menu_page(ftxui::Components rows) {
    return std::make_shared<MenuPage>(std::move(rows));
}

ftxui::ButtonOption create_plain_button_option_symbol() {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.transform = [](const ftxui::EntryState& state) {
        auto e = ftxui::bold(ftxui::text(" " + state.label + " "));
        if (state.focused) {
            return ftxui::bgcolor(ftxui::Color::White, ftxui::color(ftxui::Color::Black, e));
        }
        return e;
    };

    return option;
}

ftxui::ButtonOption create_plain_button_option() {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();

    option.transform = [](const ftxui::EntryState& state) {
        auto e = ftxui::text("< " + state.label + " >");
        if (state.focused) {
            return ftxui::bold(ftxui::inverted(e));
        }
        return e;
    };

    return option;
}

ftxui::CheckboxOption create_plain_checkbox_option() {
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
}

namespace {

    // a dropdown in Itahi's style (see create_plain_dropdown). Its own
    // component, not FTXUI's Dropdown: FTXUI's list moves with the keyboard
    // from a position that a mouse click doesn't update, so after choosing
    // with the mouse, Up / Down started from the old choice.
    class PlainDropdown : public ftxui::ComponentBase {
    public:
        PlainDropdown(const std::vector<std::string>* entries, int* selected, std::function<void()> on_change)
            : entries_(entries), selected_(selected), on_change_(std::move(on_change)) {}

        ftxui::Element OnRender() override {
            const int count = static_cast<int>(entries_->size());
            *selected_ = std::clamp(*selected_, 0, std::max(0, count - 1));
            const bool focused = Focused();
            if (!focused) {
                open_ = false;  // a list left open would hang over the rows below it
            }
            const std::string& current = count > 0 ? (*entries_)[static_cast<std::size_t>(*selected_)] : std::string();

            // every choice has the same width, so the title doesn't jump when
            // another one is picked and the list is a clean box.
            int widest = 0;
            for (const std::string& entry : *entries_) {
                widest = std::max(widest, ftxui::string_width(entry));
            }
            const auto padded = [widest](const std::string& text) {
                return text + std::string(static_cast<std::size_t>(std::max(0, widest - ftxui::string_width(text))), ' ');
            };

            // the title, like a button: "< Keep cursor centered v >".
            ftxui::Element title = ftxui::text("< " + padded(current) + (open_ ? " ^ >" : " v >"));
            if (focused) {
                title = ftxui::bold(title);
            }
            ftxui::Elements rows{title | ftxui::reflect(title_box_)};
            boxes_.resize(static_cast<std::size_t>(count));

            // open: the choices in a box under it, the current one marked
            // "(*)", the one the keys are on highlighted.
            if (open_) {
                ftxui::Elements choices;
                for (int i = 0; i < count; ++i) {
                    ftxui::Element choice = ftxui::text(std::string(i == *selected_ ? " (*) " : " ( ) ") +
                                                        padded((*entries_)[static_cast<std::size_t>(i)]) + " ");
                    if (i == highlighted_) {
                        choice = ftxui::inverted(choice) | ftxui::focus;
                    }
                    choices.push_back(choice | ftxui::reflect(boxes_[static_cast<std::size_t>(i)]));
                }
                rows.push_back(ftxui::hbox({ftxui::border(ftxui::vbox(std::move(choices))), ftxui::filler()}));
            }
            return ftxui::vbox(std::move(rows));
        }

        bool OnEvent(ftxui::Event event) override {
            const int count = static_cast<int>(entries_->size());
            if (event.is_mouse()) {
                const ftxui::Mouse& mouse = event.mouse();
                if (mouse.button != ftxui::Mouse::Left || mouse.motion != ftxui::Mouse::Pressed) {
                    return false;
                }
                if (title_box_.Contain(mouse.x, mouse.y)) {
                    TakeFocus();
                    toggle();
                    return true;
                }
                for (int i = 0; open_ && i < count; ++i) {
                    if (boxes_[static_cast<std::size_t>(i)].Contain(mouse.x, mouse.y)) {
                        TakeFocus();
                        choose(i);
                        return true;
                    }
                }
                return false;
            }
            if (!Focused()) {
                return false;
            }
            if (event == ftxui::Event::Return || event == ftxui::Event::Character(' ')) {
                if (open_) {
                    choose(highlighted_);
                } else {
                    toggle();
                }
                return true;
            }
            if (!open_) {
                return false;  // closed: the arrows move between the rows of the tab
            }
            // open: the arrows move through the choices, and stay in the list.
            if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k')) {
                highlighted_ = std::max(0, highlighted_ - 1);
                return true;
            }
            if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j')) {
                highlighted_ = std::min(count - 1, highlighted_ + 1);
                return true;
            }
            if (event == ftxui::Event::Escape) {
                open_ = false;
                return true;
            }
            return false;
        }

        bool Focusable() const override { return !entries_->empty(); }

    private:
        void toggle() {
            open_ = !open_;
            highlighted_ = *selected_;  // the keyboard starts on the current choice
        }

        void choose(int index) {
            open_ = false;
            if (index != *selected_) {
                *selected_ = index;
                if (on_change_) {
                    on_change_();
                }
            }
        }

        const std::vector<std::string>* entries_;
        int* selected_;
        std::function<void()> on_change_;
        bool open_ = false;
        int highlighted_ = 0;
        ftxui::Box title_box_;
        std::vector<ftxui::Box> boxes_;
    };

}  // namespace

ftxui::Component create_plain_dropdown(const std::vector<std::string>* entries, int* selected,
                                       std::function<void()> on_change) {
    return std::make_shared<PlainDropdown>(entries, selected, std::move(on_change));
}

// text button array (for tabs)
ftxui::Component create_text_button(const std::string& label, int tab_index, int &selected_tab, int &view_status) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        
    option.transform = [label, tab_index, &selected_tab](const ftxui::EntryState& state) {
        if (selected_tab == tab_index + 1) {
            return ftxui::bold(ftxui::text(" [" + label + "] "));
        }
        if (state.focused) {
            return ftxui::bold(ftxui::text("  " + label + "  "));
        }
        return ftxui::text("  " + label + "  ");
    };

    return ftxui::Button(label, [tab_index, &selected_tab, &view_status]() {
        if (view_status == 1) {
            view_status = 2;
        }
        selected_tab = tab_index + 1;
    }, option);
}

