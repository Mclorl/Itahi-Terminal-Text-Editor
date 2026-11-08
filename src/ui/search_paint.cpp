#include "ui/search_paint.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

#include "editor/search.hpp"

namespace {

    // the Input draws line `i` of the text on row `box.y_min + i`, one glyph
    // per cell (control characters dropped), like the syntax colors.
    class SearchPaint : public ftxui::Node {
    public:
        SearchPaint(ftxui::Element child, const std::string& text)
            : ftxui::Node({std::move(child)}), text_(text) {}

        void ComputeRequirement() override {
            ftxui::Node::ComputeRequirement();
            requirement_ = children_[0]->requirement();
        }

        void SetBox(ftxui::Box box) override {
            ftxui::Node::SetBox(box);
            children_[0]->SetBox(box);
        }

        void Render(ftxui::Screen& screen) override {
            ftxui::Node::Render(screen);

            const SearchState& search = search_state();
            if (!search.active || search.matches.empty()) {
                return;
            }

            const int y_begin = std::max(box_.y_min, screen.stencil.y_min);
            const int y_end = std::min(box_.y_max, screen.stencil.y_max);

            // start of the first visible line.
            std::size_t line_start = 0;
            for (int skip = y_begin - box_.y_min; skip > 0; --skip) {
                const std::size_t newline = text_.find('\n', line_start);
                if (newline == std::string::npos) {
                    return;
                }
                line_start = newline + 1;
            }

            for (int y = y_begin; y <= y_end && line_start <= text_.size(); ++y) {
                std::size_t line_end = text_.find('\n', line_start);
                if (line_end == std::string::npos) {
                    line_end = text_.size();
                }
                const std::string_view line = std::string_view(text_).substr(line_start, line_end - line_start);

                const std::vector<std::string> glyphs = ftxui::Utf8ToGlyphs(line);
                std::size_t byte = 0;
                int kind = 0;
                for (std::size_t cell = 0; cell < glyphs.size(); ++cell) {
                    if (!glyphs[cell].empty()) {
                        const std::size_t found = line.find(glyphs[cell], byte);
                        if (found == std::string_view::npos) {
                            break;
                        }
                        byte = found + glyphs[cell].size();
                        kind = search_match_kind(line_start + found);
                    }
                    const int x = box_.x_min + static_cast<int>(cell);
                    if (kind == 0 || x < screen.stencil.x_min) {
                        continue;
                    }
                    if (x > screen.stencil.x_max) {
                        break;
                    }
                    ftxui::Cell& c = screen.CellAt(x, y);
                    c.background_color = kind == 2 ? search_current_background : search_match_background;
                    c.foreground_color = search_match_foreground;
                }
                line_start = line_end + 1;
            }
        }

    private:
        const std::string& text_;
    };

}  // namespace

ftxui::Decorator search_highlight(const std::string& text) {
    return [&text](ftxui::Element child) -> ftxui::Element {
        return std::make_shared<SearchPaint>(std::move(child), text);
    };
}
