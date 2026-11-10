#include "ui/menu_pages.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
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
#include "ui/help.hpp"
#include "ui/ui_state.hpp"
#include "ui/widgets.hpp"

namespace {

    void show(const CommandResult& result) {
        (result.ok ? command_output : command_error) = result.message;
    }

    // a button row: "< Label >" and, dim on its right, how else to do it.
    ftxui::Component button(const std::string& label, std::function<void()> action, std::string hint = "") {
        return menu_row(ftxui::Button(label, std::move(action), create_plain_button_option()), std::move(hint));
    }

    // a button that runs a command, the way typing it would.
    ftxui::Component command_button(const std::string& label, const std::string& command, std::string hint = "") {
        return button(label, [command] { show(run_command(command)); }, std::move(hint));
    }

    ftxui::Component checkbox(const std::string& label, bool* value) {
        return menu_row(ftxui::Checkbox(label, value, create_plain_checkbox_option()));
    }

    // a dropdown with its label in front: "Theme            < Itahi v >".
    ftxui::Component dropdown(const std::string& label, const std::vector<std::string>* entries, int* selected,
                              std::function<void()> on_change = {}) {
        ftxui::Component control = create_plain_dropdown(entries, selected, std::move(on_change));
        return menu_row(ftxui::Renderer(control, [label, control] {
            return ftxui::hbox({menu_label(label), control->Render()});
        }));
    }

    // one View tab row: "Keyword       < Yellow >". Left/Right (or h/l) change
    // the color, Enter or a click moves to the next one.
    class SyntaxColorRow : public ftxui::ComponentBase {
    public:
        explicit SyntaxColorRow(SyntaxGroup group) : group_(static_cast<int>(group)) {}

        ftxui::Element OnRender() override {
            const GroupSetting& setting = group_settings()[group_];
            const NamedColor& named = color_palette()[static_cast<std::size_t>(setting.color_index)];

            ftxui::Element sample = ftxui::text(named.name);
            if (setting.color_index != 0) {
                sample = ftxui::color(named.color, sample);
            }
            ftxui::Element e = ftxui::hbox({menu_label(setting.label), ftxui::text("< "), sample, ftxui::text(" >")});
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

    // a line of text between rows, with the rows' margin.
    ftxui::Component note(std::function<std::string()> text) {
        return ftxui::Renderer([text = std::move(text)] {
            return ftxui::hbox({ftxui::text("   "), ftxui::dim(ftxui::text(text()))});
        });
    }

}  // namespace

ftxui::Component file_back_button_func() {
    return ftxui::Button("<-", [] {
        status = 1;
        tab_selected = 0;
    }, create_plain_button_option_symbol());
}

ftxui::Component make_file_page() {
    return menu_page({
        menu_title("File"),
        // ">e src/" with the active folder (the one selected in the explorer)
        // written in: type the name. Ctrl+N does the same.
        button("New File", [] {
            if (open_command_line) {
                open_command_line("e " + active_folder_for_command());
            }
        }, "Ctrl+N"),
        menu_gap(),
        button("Save File", [] { show(save_active_file()); }, "Ctrl+S   >w"),
        menu_gap(),
        command_button("Save All Files", "wa", ">wa"),
        menu_gap(),
        button("Close File", [] { show(close_active_file()); }, ">bd"),
        menu_title("Folder"),
        button("New Folder", [] {
            if (open_command_line) {
                open_command_line("mkdir " + active_folder_for_command());
            }
        }, ">mkdir name"),
        menu_gap(),
        command_button("Read Folders Again", "refresh", "F5"),
        menu_title("Itahi"),
        command_button("Quit", "q", ">q   Ctrl+C"),
    });
}

ftxui::Component make_edit_page(std::function<EditorView&()> editor) {
    return menu_page({
        menu_title("Edit"),
        button("Undo", [] { show(undo_edit()); }, "Ctrl+Z   u"),
        menu_gap(),
        button("Redo", [] { show(redo_edit()); }, "Ctrl+Y   Ctrl+R"),
        menu_title("Clipboard"),
        button("Cut", [editor] { editor().cut(); }, "Ctrl+X   d"),
        menu_gap(),
        button("Copy", [editor] { editor().copy(); }, "Ctrl+C   y"),
        menu_gap(),
        button("Paste", [editor] { editor().paste_clipboard(); }, "Ctrl+V   p"),
        menu_gap(),
        button("Select All", [editor] { editor().select_all(); }, "Ctrl+A"),
    });
}

ftxui::Component make_view_page() {
    // the order matches ROW_MARK_WHITE_LINE / ROW_MARK_SHADED / ROW_MARK_TEXT_COLOR.
    static const std::vector<std::string> row_styles = {"White underline", "Shaded background", "Underline in text color"};
    static const std::vector<std::string> theme_names = [] {
        std::vector<std::string> list;
        for (const Theme& theme : themes()) {
            list.emplace_back(theme.name);
        }
        return list;
    }();

    ftxui::Components rows = {
        menu_title("Windows"),
        command_button("Split Right", "vsplit", "w v   >vsplit"),
        menu_gap(),
        command_button("Split Down", "split", "w s   >split"),
        menu_gap(),
        command_button("Close Window", "close", "w c   >close"),
        menu_gap(),
        command_button("Close Other Windows", "only", "w o   >only"),
        menu_gap(),
        command_button("Move Window Left", "wmove left", "Alt+Shift+Left"),
        command_button("Move Window Right", "wmove right", "Alt+Shift+Right"),
        command_button("Move Window Up", "wmove up", "Alt+Shift+Up"),
        command_button("Move Window Down", "wmove down", "Alt+Shift+Down"),

        menu_title("Editor"),
        checkbox("Show Line Numbers", &settings.show_line_numbers),
        checkbox("Show Underline On Active Row On Numbers", &settings.underline_active_row_on_numbers),
        checkbox("Show Underline On Active Row", &settings.underline_active_row),
        checkbox("Enable Syntax Highlighting", &settings.syntax_highlighting),
        checkbox("Enable Word Wrap", &settings.word_wrap),
        checkbox("Show Status Bar", &settings.show_status_bar),
        checkbox("Show Open File Tabs", &settings.show_tabs),

        menu_title("Appearance"),
        dropdown("Theme", &theme_names, &settings.theme, [] { apply_theme(settings.theme); }),
        menu_gap(),
        dropdown("Active Row Style", &row_styles, &settings.active_row_mark),

        menu_title("Syntax Colors"),
        note([] {
            const std::string language = syntax_language_name(active_path());
            return "Current file: " + (language.empty() ? std::string("no highlighting") : language) +
                   ".  Left / Right or a click changes a color.";
        }),
        menu_gap(),
    };
    for (int group = 1; group < group_count; ++group) {
        rows.push_back(menu_row(std::make_shared<SyntaxColorRow>(static_cast<SyntaxGroup>(group))));
    }
    return menu_page(std::move(rows));
}

ftxui::Component make_settings_page() {
    // the order matches SCROLL_KEEP_CURSOR_CENTERED / SCROLL_FREELY.
    static const std::vector<std::string> scroll_styles = {"Keep cursor centered", "Scroll freely"};
    return menu_page({
        menu_title("Editing"),
        menu_row(make_numeric_input_row(settings.tab_size, "Tab Size (columns)", MAX_DIGITS_LENGTH_DEFAULT, MAX_WIDTH_INPUT_INT_DEFAULT), "1 - 16"),
        menu_gap(),
        checkbox("Tab Key Inserts Spaces", &settings.tab_inserts_spaces),
        menu_title("Mouse and scrolling"),
        menu_row(make_numeric_input_row(settings.mouse_sensitivity, "Mouse Sensitivity", MAX_DIGITS_LENGTH_DEFAULT, MAX_WIDTH_INPUT_INT_DEFAULT), "rows per wheel notch"),
        menu_gap(),
        dropdown("Scroll Style", &scroll_styles, &settings.scroll_style),
        menu_title("Search"),
        checkbox("Ignore Upper / Lower Case", &settings.search_ignore_case),
        checkbox("Whole Words Only", &settings.search_whole_word),
        menu_title("Files"),
        checkbox("Keep Unsaved Changes For Recovery", &settings.keep_recovery_copies),
        checkbox("Read Files Again When They Change On Disk", &settings.auto_reload),
    });
}

ftxui::Component make_help_page() {
    // the list takes the room there is and scrolls by itself; the two
    // buttons stay under it.
    ftxui::Component list = make_help_view();
    ftxui::Component about_itahi = ftxui::Button("About Itahi Editor", [] {
        if (!open_url("https://github.com/Mclorl/Itahi-Terminal-Text-Editor")) {
            command_error = "Could not open the Itahi Editor repository in a browser";
        }
    }, create_plain_button_option());
    ftxui::Component about_ftxui = ftxui::Button("About FTXUI", [] {
        if (!open_url("https://github.com/ArthurSonzogni/FTXUI")) {
            command_error = "Could not open the FTXUI repository in a browser";
        }
    }, create_plain_button_option());
    ftxui::Component buttons = ftxui::Container::Horizontal({about_itahi, about_ftxui});
    ftxui::Component page = ftxui::Container::Vertical({list, buttons});
    return ftxui::Renderer(page, [list, about_itahi, about_ftxui] {
        return ftxui::vbox({
            ftxui::text(""),
            ftxui::hbox({ftxui::text(list->Focused() ? " ┃ " : "   "), ftxui::bold(ftxui::text("Keys and commands")),
                         ftxui::dim(ftxui::text("   arrows, Page keys or the wheel scroll"))}),
            ftxui::text(""),
            ftxui::hbox({ftxui::text("   "), list->Render() | ftxui::flex, ftxui::text(" ")}) | ftxui::flex,
            ftxui::text(""),
            ftxui::hbox({ftxui::text("   "), about_itahi->Render(), ftxui::text("   "), about_ftxui->Render(),
                         ftxui::dim(ftxui::text("   (websites)"))}),
            ftxui::text(""),
        }) | ftxui::flex;
    });
}
