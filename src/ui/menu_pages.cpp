#include "ui/menu_pages.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "syntax/highlighter.hpp"
#include "syntax/theme.hpp"

#include "editor/commands.hpp"
#include "editor/state.hpp"
#include "editor/undo_history.hpp"
#include "platform/open_url.hpp"
#include "ui/command_line.hpp"
#include "ui/ui_state.hpp"
#include "ui/widgets.hpp"

// tab functions
// --- File Back Option ---
ftxui::Component file_back_button_func() {
    return ftxui::Button("<-", [] {
        status = 1;
        tab_selected = 0;
    }, create_plain_button_option_symbol());
}
// --- File Menu Choices ---
ftxui::Component file_btn_new_file_func() {
    return ftxui::Button("New File", [] {
        // ">e folder/name" creates the file. The folder is the one selected in
        // the explorer (or the folder of the selected file); type the name.
        if (!open_command_line) {
            return;
        }
        std::string folder;
        if (!explorer_selected_path.empty()) {
            folder = explorer_selected_is_folder ? explorer_selected_path
                                                 : std::filesystem::path(explorer_selected_path).parent_path().string();
        }
        open_command_line("e " + (folder.empty() ? std::string() : folder + "/"));
    }, create_plain_button_option());
}

ftxui::Component file_btn_save_file_func() {
    return ftxui::Button("Save File", [] {
        const CommandResult result = save_active_file();
        (result.ok ? command_output : command_error) = result.message;
    }, create_plain_button_option());
}
// --- Edit Menu Choices ---
ftxui::Component edit_btn_undo_func() {
    return ftxui::Button("Undo", [] {
        const CommandResult result = undo_edit();
        (result.ok ? command_output : command_error) = result.message;
    }, create_plain_button_option());
}
ftxui::Component edit_btn_redo_func() {
    return ftxui::Button("Redo", [] {
        const CommandResult result = redo_edit();
        (result.ok ? command_output : command_error) = result.message;
    }, create_plain_button_option());
}
// --- View Menu Choices ---
ftxui::Component toggle_linenums_func() {
    return ftxui::Checkbox("Show Line Numbers", &show_line_numbers, create_plain_checkbox_option());
}

ftxui::Component toggle_underline_active_row_on_numbers_func() {
    return ftxui::Checkbox("Show Underline On Active Row On Numbers", &underline_active_row_on_numbers, create_plain_checkbox_option());
}

ftxui::Component toggle_underline_active_row_func() {
    return ftxui::Checkbox("Show Underline On Active Row", &underline_active_row, create_plain_checkbox_option());
}

ftxui::Component toggle_syntax_highlighting_func() {
    return ftxui::Checkbox("Enable Syntax Highlighting", &syntax_highlighting, create_plain_checkbox_option());
}

ftxui::Component toggle_word_wrap_func() {
    return ftxui::Checkbox("Enable Word Wrap", &word_wrap, create_plain_checkbox_option());
}

// --- Settings Menu Choices ---
ftxui::Component mouse_sens_row_func() {
    return make_numeric_input_row(mouse_sensitivity, "Mouse Sensitivity: ", MAX_DIGITS_LENGTH_DEFAULT, MAX_WIDTH_INPUT_INT_DEFAULT);
}
ftxui::Component tab_size_row_func() {
    return make_numeric_input_row(tab_size, "Tab Size (spaces): ", MAX_DIGITS_LENGTH_DEFAULT, MAX_WIDTH_INPUT_INT_DEFAULT);
}
// --- Help Menu Choices ---
ftxui::Component btn_about_func() {
    return ftxui::Button("About Itahi Editor", [] {
        if (!open_url("https://github.com/Mclorl/Itahi-Terminal-Text-Editor")) {
            command_error = "Could not open the Itahi Editor repository in a browser";
        }
    }, create_plain_button_option());
}
ftxui::Component btn_ftxui_about_func() {
    return ftxui::Button("About FTXUI", [] {
        if (!open_url("https://github.com/ArthurSonzogni/FTXUI")) {
            command_error = "Could not open the FTXUI repository in a browser";
        }
    }, create_plain_button_option());
}

ftxui::Component warning_text_link_direct_func() {
    // an information row: the buttons below it open a website.
    return ftxui::Button("Once clicked it will direct you to a website!", [] {}, create_plain_button_option());
}

namespace {

    // one View tab row: "Keyword       < Yellow >". Left/Right (or h/l) change
    // the color, Enter or a click moves to the next one.
    class SyntaxColorRow : public ftxui::ComponentBase {
    public:
        explicit SyntaxColorRow(SyntaxGroup group) : group_(static_cast<int>(group)) {}

        ftxui::Element OnRender() override {
            const GroupSetting& setting = group_settings()[group_];
            const NamedColor& named = color_palette()[static_cast<std::size_t>(setting.color_index)];

            std::string label = setting.label;
            label.resize(std::max<std::size_t>(label.size(), 14), ' ');

            ftxui::Element sample = ftxui::text(named.name);
            if (setting.color_index != 0) {
                sample = ftxui::color(named.color, sample);
            }

            ftxui::Element e = ftxui::hbox({ftxui::text(label), ftxui::text("< "), sample, ftxui::text(" >")});
            if (Focused()) {
                e = ftxui::bold(e);
            }
            return e | ftxui::reflect(box_);
        }

        bool OnEvent(ftxui::Event event) override {
            if (event.is_mouse()) {
                const ftxui::Mouse& mouse = event.mouse();
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed && box_.Contain(mouse.x, mouse.y)) {
                    TakeFocus();
                    cycle(+1);
                    return true;
                }
                return false;
            }

            if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Character('l') || event == ftxui::Event::Return) {
                cycle(+1);
                return true;
            }
            if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::Character('h')) {
                cycle(-1);
                return true;
            }
            return false;
        }

        bool Focusable() const override { return true; }

    private:
        void cycle(int step) {
            const int size = static_cast<int>(color_palette().size());
            int& index = group_settings()[group_].color_index;
            index = (index + step + size) % size;
        }

        int group_;
        ftxui::Box box_;
    };

}  // namespace

ftxui::Component syntax_color_settings_func() {
    ftxui::Components rows;
    for (int group = 1; group < group_count; ++group) {
        auto row = std::make_shared<SyntaxColorRow>(static_cast<SyntaxGroup>(group));

        rows.push_back(ftxui::Renderer(row, [row] {
            return ftxui::hbox({
                ftxui::text("    "),// indent
                row->Render(),
            });
        }));
    }
    ftxui::Component rows_container = ftxui::Container::Vertical(std::move(rows));

    // the header is drawn around the rows (not a row itself), so Up/Down
    // never stop on it.
    return ftxui::Renderer(rows_container, [rows_container] {
        const std::string language = syntax_language_name(long_active_file_name);
        return ftxui::vbox({
            ftxui::text(""),
            ftxui::bold(ftxui::text("Syntax Colors")),
            ftxui::dim(ftxui::text("Current file: " + (language.empty() ? std::string("no highlighting") : language))),
            ftxui::dim(ftxui::text("Left / Right or click to change a color.")),
            ftxui::text(""),
            rows_container->Render(),
        });
    });
}
