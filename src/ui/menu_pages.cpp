#include "ui/menu_pages.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "syntax/highlighter.hpp"
#include "syntax/theme.hpp"

#include "editor/commands.hpp"
#include "editor/paths.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/undo_history.hpp"
#include "editor/workspace.hpp"
#include "platform/open_url.hpp"
#include "ui/command_line.hpp"
#include "ui/editor_view.hpp"
#include "ui/ui_state.hpp"
#include "ui/widgets.hpp"

namespace {

    void show(const CommandResult& result) {
        (result.ok ? command_output : command_error) = result.message;
    }

    // a dropdown with its label in front: "Theme: < Itahi v >".
    ftxui::Component labeled(const std::string& label, ftxui::Component dropdown) {
        return ftxui::Renderer(dropdown, [label, dropdown] {
            return ftxui::hbox({ftxui::text(label), dropdown->Render()});
        });
    }

}  // namespace

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
        // ">e src/" with the active folder (the one selected in the explorer)
        // written in: type the name. Ctrl+N does the same.
        if (open_command_line) {
            open_command_line("e " + active_folder_for_command());
        }
    }, create_plain_button_option());
}

ftxui::Component file_btn_save_file_func() {
    return ftxui::Button("Save File", [] { show(save_active_file()); }, create_plain_button_option());
}

ftxui::Component file_btn_close_file_func() {
    return ftxui::Button("Close File", [] { show(close_active_file()); }, create_plain_button_option());
}
// --- Edit Menu Choices ---
ftxui::Component edit_btn_undo_func() {
    return ftxui::Button("Undo", [] { show(undo_edit()); }, create_plain_button_option());
}
ftxui::Component edit_btn_redo_func() {
    return ftxui::Button("Redo", [] { show(redo_edit()); }, create_plain_button_option());
}
ftxui::Component edit_btn_cut_func(EditorView& view) {
    return ftxui::Button("Cut", [&view] { view.cut(); }, create_plain_button_option());
}
ftxui::Component edit_btn_copy_func(EditorView& view) {
    return ftxui::Button("Copy", [&view] { view.copy(); }, create_plain_button_option());
}
ftxui::Component edit_btn_paste_func(EditorView& view) {
    return ftxui::Button("Paste", [&view] { view.paste_clipboard(); }, create_plain_button_option());
}
ftxui::Component edit_btn_select_all_func(EditorView& view) {
    return ftxui::Button("Select All", [&view] { view.select_all(); }, create_plain_button_option());
}
// --- View Menu Choices ---
ftxui::Component toggle_linenums_func() {
    return ftxui::Checkbox("Show Line Numbers", &settings.show_line_numbers, create_plain_checkbox_option());
}

ftxui::Component toggle_underline_active_row_on_numbers_func() {
    return ftxui::Checkbox("Show Underline On Active Row On Numbers", &settings.underline_active_row_on_numbers, create_plain_checkbox_option());
}

ftxui::Component toggle_underline_active_row_func() {
    return ftxui::Checkbox("Show Underline On Active Row", &settings.underline_active_row, create_plain_checkbox_option());
}

ftxui::Component active_row_style_row_func() {
    // the order matches ROW_MARK_WHITE_LINE / ROW_MARK_SHADED / ROW_MARK_TEXT_COLOR.
    static const std::vector<std::string> styles = {"White underline", "Shaded background", "Underline in text color"};
    return labeled("Active Row Style: ", create_plain_dropdown(&styles, &settings.active_row_mark));
}

ftxui::Component toggle_syntax_highlighting_func() {
    return ftxui::Checkbox("Enable Syntax Highlighting", &settings.syntax_highlighting, create_plain_checkbox_option());
}

ftxui::Component toggle_word_wrap_func() {
    return ftxui::Checkbox("Enable Word Wrap", &settings.word_wrap, create_plain_checkbox_option());
}

ftxui::Component toggle_status_bar_func() {
    return ftxui::Checkbox("Show Status Bar", &settings.show_status_bar, create_plain_checkbox_option());
}

ftxui::Component toggle_tabs_func() {
    return ftxui::Checkbox("Show Open File Tabs", &settings.show_tabs, create_plain_checkbox_option());
}

ftxui::Component theme_row_func() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> list;
        for (const Theme& theme : themes()) {
            list.emplace_back(theme.name);
        }
        return list;
    }();
    return labeled("Theme: ", create_plain_dropdown(&names, &settings.theme, [] { apply_theme(settings.theme); }));
}

// --- Settings Menu Choices ---
ftxui::Component mouse_sens_row_func() {
    return make_numeric_input_row(settings.mouse_sensitivity, "Mouse Sensitivity: ", MAX_DIGITS_LENGTH_DEFAULT, MAX_WIDTH_INPUT_INT_DEFAULT);
}
ftxui::Component tab_size_row_func() {
    return make_numeric_input_row(settings.tab_size, "Tab Size (columns): ", MAX_DIGITS_LENGTH_DEFAULT, MAX_WIDTH_INPUT_INT_DEFAULT);
}
ftxui::Component toggle_tab_inserts_spaces_func() {
    return ftxui::Checkbox("Tab Key Inserts Spaces", &settings.tab_inserts_spaces, create_plain_checkbox_option());
}
ftxui::Component scroll_style_row_func() {
    // the order matches SCROLL_KEEP_CURSOR_CENTERED / SCROLL_FREELY.
    static const std::vector<std::string> styles = {"Keep cursor centered", "Scroll freely"};
    return labeled("Scroll style: ", create_plain_dropdown(&styles, &settings.scroll_style));
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
        const std::string language = syntax_language_name(active_path());
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
