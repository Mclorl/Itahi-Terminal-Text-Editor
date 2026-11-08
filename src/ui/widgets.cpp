
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
        const int size_inner = box_.y_max - box_.y_min;
        const int size_outer = stencil.y_max - stencil.y_min + 1;
        const int x = stencil.x_max;

        for (int y = stencil.y_min; y <= stencil.y_max; ++y) {
            ftxui::Cell& cell = screen.CellAt(x, y);
            cell.character = " ";
            cell.foreground_color = ftxui::Color::White;
            cell.background_color = ftxui::Color::Default;
            cell.bold = cell.dim = cell.italic = cell.underlined = cell.inverted = false;
        }
        if (size_inner <= 0 || size_outer >= size_inner) {
            return;  // everything fits: no bar
        }

        const int size = std::max(2 * size_outer * size_outer / size_inner, 1);
        const int start_y = 2 * stencil.y_min + 2 * (stencil.y_min - box_.y_min) * size_outer / size_inner;

        for (int y = stencil.y_min; y <= stencil.y_max; ++y) {
            const bool up = start_y <= 2 * y && 2 * y <= start_y + size;
            const bool down = start_y <= 2 * y + 1 && 2 * y + 1 <= start_y + size;
            screen.CellAt(x, y).character = up ? (down ? "┃" : "╹") : (down ? "╻" : " ");
        }
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

namespace {

    // the terminal draws an underline in the text's color, so on a
    // highlighted line it changes color with every word. Terminals also have
    // a separate underline color (SGR 58), which FTXUI doesn't know about:
    // it is sent in front of the cell's character instead. FTXUI never resets
    // all attributes at once, so it stays set. Full-width characters are
    // skipped (FTXUI measures them to know which cell to skip next); they keep
    // the color set by the cell before them.
    void underline_white(ftxui::Screen& screen, int x, int y, int x_min) {
        ftxui::Cell& cell = screen.CellAt(x, y);
        cell.underlined = true;
        const bool second_half = cell.character.empty() && x > x_min &&
                                 ftxui::string_width(screen.CellAt(x - 1, y).character) == 2;
        if (second_half || ftxui::string_width(cell.character) == 2) {
            return;
        }
        cell.character = "\x1B[58;5;15m" + (cell.character.empty() ? std::string(" ") : cell.character);
    }

    // underlines every cell of the child (one row of the wrapped view).
    class UnderlineAll : public ftxui::Node {
    public:
    explicit UnderlineAll(ftxui::Element child) : ftxui::Node({std::move(child)}) {}

    void ComputeRequirement() override {
        ftxui::Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
    }

    void SetBox(ftxui::Box box) override {
        ftxui::Node::SetBox(box);
        children_[0]->SetBox(box);
    }

    void Render(ftxui::Screen& screen) override {
        children_[0]->Render(screen);
        const int x_min = std::max(box_.x_min, screen.stencil.x_min);
        for (int y = std::max(box_.y_min, screen.stencil.y_min); y <= std::min(box_.y_max, screen.stencil.y_max); ++y) {
            for (int x = x_min; x <= std::min(box_.x_max, screen.stencil.x_max); ++x) {
                underline_white(screen, x, y, x_min);
            }
        }
    }
    };

    // underlines one row of the child: the cursor's line in the editor.
    class UnderlineRow : public ftxui::Node {
    public:
    UnderlineRow(ftxui::Element child, int row) : ftxui::Node({std::move(child)}), row_(row) {}

    void ComputeRequirement() override {
        ftxui::Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
    }

    void SetBox(ftxui::Box box) override {
        ftxui::Node::SetBox(box);
        children_[0]->SetBox(box);
    }

    void Render(ftxui::Screen& screen) override {
        children_[0]->Render(screen);
        const int y = box_.y_min + row_;
        if (row_ < 0 || y < screen.stencil.y_min || y > screen.stencil.y_max || y > box_.y_max) {
            return;
        }
        const int x_min = std::max(box_.x_min, screen.stencil.x_min);
        for (int x = x_min; x <= std::min(box_.x_max, screen.stencil.x_max); ++x) {
            underline_white(screen, x, y, x_min);
        }
    }

    private:
    int row_;
    };

} // namespace

ftxui::Element underlined_white(ftxui::Element child) {
    return std::make_shared<UnderlineAll>(std::move(child));
}

ftxui::Decorator underline_row(int row) {
    return [row](ftxui::Element child) -> ftxui::Element {
        return std::make_shared<UnderlineRow>(std::move(child), row);
    };
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

    return ftxui::Renderer(input_num, [input_num, label_text, max_digits_taken, max_width]() {
        return ftxui::hbox({
            ftxui::text(label_text),
            ftxui::underlined(
                ftxui::size(ftxui::WIDTH, ftxui::EQUAL, max_width)(input_num->Render())
            )
        });
    });
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
            return ftxui::bold(e);
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

// text button array (for tabs)
ftxui::Component create_text_button(const std::string& label, int tab_index, int &selected_tab, int &view_status) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        
    option.transform = [label, tab_index, &selected_tab](const ftxui::EntryState& state) {
        if (selected_tab == tab_index + 1) {
            return ftxui::bold(
                ftxui::text("[" + label + "]")
            );
        } 
        if (state.focused) {
            return ftxui::bold(
                ftxui::text(" " + label + " ")
            );
        }
        return ftxui::text(" " + label + " ");
    };

    return ftxui::Button(label, [tab_index, &selected_tab, &view_status]() {
        if (view_status == 1) {
            view_status = 2;
        }
        selected_tab = tab_index + 1;
    }, option);
}

