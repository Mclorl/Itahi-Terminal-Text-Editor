#include "ui/help.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/string.hpp>

#include "editor/commands.hpp"
#include "ui/widgets.hpp"

namespace {

    struct KeyHelp {
        const char* group;
        const char* keys;
        const char* help;
    };

    const KeyHelp key_help[] = {
        {"Moving around", "Ctrl + arrows  (or Ctrl + H J K L)", "Normal mode: to the window, the explorer or the menu bar on that side"},
        {"Moving around", "F6  /  Shift+F6", "The next / previous part: explorer, then every window"},
        {"Moving around", "Alt+Left  /  Alt+Right", "The previous / next open file (also Ctrl+Page Up / Page Down)"},
        {"Moving around", "F1", "This list"},

        {"Windows", "w, then v   /   Ctrl+\\", "Split: a new window on the right (Normal mode, like all the w keys)"},
        {"Windows", "w, then s", "Split: a new window below (four windows at most)"},
        {"Windows", "w, then arrows or h j k l", "Go to the window on that side (w: the next one, 1-4: by number)"},
        {"Windows", "w, then Alt+arrows or H J K L", "Move the window to that side (also Alt+Shift+arrows; Shift+arrows where the terminal lets them through)"},
        {"Windows", "w, then c  /  o", "Close the window  /  close all the others"},
        {"Windows", "w, then =  <  >  +  -", "The same size again  /  narrower, wider, taller, shorter"},
        {"Windows", ">vsplit  /  >split", "The same from the command line (>w saves, it is not a window key there)"},
        {"Windows", "Mouse", "A click gives a window the focus; drag the line between two to resize"},

        {"Explorer", "Up  Down  (j k)", "Move through the rows; they never leave the explorer"},
        {"Explorer", "Enter", "Open the file, or open / close the folder"},
        {"Explorer", "Right  /  Left  (l h)", "Open a folder, go into it  /  close it, go to the folder above"},
        {"Explorer", "F2  /  Delete", "Rename  /  move to the trash (asked on the command line first)"},
        {"Explorer", "+", "Marks the folder Ctrl+N makes new files in: the selected folder, or the folder of the selected file"},

        {"Editing", "e  or  i   /   Esc", "Edit mode (type in the file)  /  back to Normal mode"},
        {"Editing", "Arrows, Home, End, Page keys", "Move the cursor; with Shift they select"},
        {"Editing", "Ctrl+Home / End,  Ctrl+Left / Right", "Start / end of the file,  previous / next word"},
        {"Editing", "v   y   d or x   p", "Normal mode: select, copy, cut, paste"},
        {"Editing", "Ctrl+A   Ctrl+C   Ctrl+X   Ctrl+V", "Select all, copy, cut, paste"},
        {"Editing", "Ctrl+Z  /  Ctrl+Y   (u  /  Ctrl+R)", "Undo  /  redo"},
        {"Editing", "Tab  /  Shift+Tab", "Edit mode: indent / unindent"},
        {"Editing", "/  ?   then  n  N", "Search down / up, the next / previous match"},
        {"Editing", "Alt+C  /  Alt+W  (while searching)", "Ignore case  /  whole words only"},

        {"Files", "Ctrl+S", "Save"},
        {"Files", "Ctrl+N", "New file in the folder marked +"},
        {"Files", "F5", "Read the folders, the open files and Git again"},
        {"Files", "Ctrl+C", "Copy the selection; with none, quit (never with unsaved changes)"},

        {"Command line", ">", "Normal mode: open it. Enter runs, Esc closes"},
        {"Command line", "Up  /  Down", "The commands run before"},
        {"Command line", "Tab", "Complete a file or folder name"},
    };

    // the list, scrolled with the arrows, the page keys and the wheel.
    class HelpView : public ftxui::ComponentBase {
    public:
        HelpView() {
            const auto add = [this](const std::string& group, const std::string& left, const std::string& right) {
                if (group != last_group_) {
                    if (!last_group_.empty()) {
                        lines_.push_back(ftxui::text(""));
                    }
                    lines_.push_back(ftxui::bold(ftxui::text(group)));
                    last_group_ = group;
                }
                // one text per row, its first column padded by hand: a narrow
                // window cuts the row at its end instead of squeezing the columns.
                constexpr int key_columns = 38;
                std::string keys = "  " + left;
                keys += std::string(static_cast<std::size_t>(std::max(2, key_columns - ftxui::string_width(keys))), ' ');
                lines_.push_back(ftxui::text(keys + right));
            };
            for (const KeyHelp& row : key_help) {
                add(row.group, row.keys, row.help);
            }
            last_group_.clear();
            lines_.push_back(ftxui::text(""));
            lines_.push_back(ftxui::bold(ftxui::text("Commands: press > and type one, then Enter")));
            for (const CommandHelp& row : command_help()) {
                add("  " + row.group, "  >" + row.usage, row.help);
            }
        }

        bool Focusable() const override { return true; }

        ftxui::Element OnRender() override {
            return scroll_frame(white_vscroll_indicator(ftxui::vbox(lines_)), scroll_) | ftxui::flex | ftxui::reflect(box_);
        }

        bool OnEvent(ftxui::Event event) override {
            const int before = scroll_.top;
            if (event.is_mouse()) {
                const ftxui::Mouse& mouse = event.mouse();
                if (!box_.Contain(mouse.x, mouse.y)) {
                    return false;
                }
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    TakeFocus();
                    return true;
                }
                if (mouse.button == ftxui::Mouse::WheelUp || mouse.button == ftxui::Mouse::WheelDown) {
                    scroll_.scroll_by(mouse.button == ftxui::Mouse::WheelUp ? -3 : 3);
                    return true;  // the wheel is the list's, also at its ends
                }
                return false;
            }
            // at either end the key moves on to the row above or below.
            if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k')) scroll_.scroll_by(-1);
            else if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j')) scroll_.scroll_by(1);
            else if (event == ftxui::Event::PageUp) scroll_.scroll_by(-scroll_.view_rows);
            else if (event == ftxui::Event::PageDown) scroll_.scroll_by(scroll_.view_rows);
            else if (event == ftxui::Event::Home) scroll_.scroll_by(-scroll_.content_rows);
            else if (event == ftxui::Event::End) scroll_.scroll_by(scroll_.content_rows);
            return scroll_.top != before;
        }

    private:
        ftxui::Elements lines_;
        std::string last_group_;
        ScrollState scroll_;
        ftxui::Box box_;
    };

}  // namespace

ftxui::Component make_help_view() {
    return std::make_shared<HelpView>();
}
