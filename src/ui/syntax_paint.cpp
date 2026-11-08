#include "ui/syntax_paint.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

#include "editor/state.hpp"
#include "syntax/highlighter.hpp"
#include "syntax/theme.hpp"

namespace {

    // renders the Input, then recolors its cells. The Input draws line `i` of
    // the content on row `box.y_min + i`, one glyph per cell.
    class SyntaxPaint : public ftxui::Node {
    public:
        SyntaxPaint(ftxui::Element child, const std::string& content, const std::string& path)
            : ftxui::Node({std::move(child)}), content_(content), path_(path) {}

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

            if (!syntax_highlighting || content_.empty()) {
                return;
            }

            HighlightCache& cache = highlights_for(content_, path_);
            if (cache.language_index < 0) {
                return; // no grammar for this file.
            }

            // only the rows/columns visible through the surrounding frame.
            const int y_begin = std::max(box_.y_min, screen.stencil.y_min);
            const int y_end = std::min(box_.y_max, screen.stencil.y_max);

            const std::size_t first_line = static_cast<std::size_t>(std::max(0, y_begin - box_.y_min));
            const std::size_t last_line = static_cast<std::size_t>(std::max(0, y_end - box_.y_min)) + 1;
            if (first_line >= cache.line_starts.size()) {
                return;
            }
            const std::size_t visible_begin = cache.line_starts[first_line];
            const std::size_t visible_end = last_line < cache.line_starts.size() ? cache.line_starts[last_line] : content_.size();
            ensure_colored(cache, visible_begin, visible_end);

            const auto& settings = group_settings();
            const auto& palette = color_palette();

            for (int y = y_begin; y <= y_end; ++y) {
                const std::size_t line = static_cast<std::size_t>(y - box_.y_min);
                if (line >= cache.line_starts.size()) {
                    break;
                }

                const std::size_t line_start = cache.line_starts[line];
                const std::size_t line_end = line + 1 < cache.line_starts.size() ? cache.line_starts[line + 1] - 1 : content_.size();
                const std::string_view line_text = std::string_view(content_).substr(line_start, line_end - line_start);

                // walk the glyphs exactly like the Input's text() does: control
                // characters (eg. tabs) are dropped and full width glyphs take a
                // second, empty cell. Find each glyph's byte to get its group.
                const std::vector<std::string> glyphs = ftxui::Utf8ToGlyphs(line_text);
                std::size_t byte = 0;
                unsigned char group = 0;

                for (std::size_t cell = 0; cell < glyphs.size(); ++cell) {
                    const std::string& glyph = glyphs[cell];
                    if (!glyph.empty()) {
                        const std::size_t found = line_text.find(glyph, byte);
                        if (found == std::string_view::npos) {
                            break;
                        }
                        byte = found + glyph.size();
                        group = cache.byte_groups[line_start + found];
                    }

                    const int x = box_.x_min + static_cast<int>(cell);
                    if (x < screen.stencil.x_min) {
                        continue;
                    }
                    if (x > screen.stencil.x_max) {
                        break;
                    }

                    const int color_index = settings[group].color_index;
                    if (group != 0 && color_index != 0) {
                        screen.CellAt(x, y).foreground_color = palette[static_cast<std::size_t>(color_index)].color;
                    }
                }
            }
        }

    private:
        const std::string& content_;
        const std::string& path_;
    };

}  // namespace

ftxui::Decorator syntax_highlight(const std::string& text, const std::string& path) {
    return [&text, &path](ftxui::Element child) -> ftxui::Element {
        return std::make_shared<SyntaxPaint>(std::move(child), text, path);
    };
}
