#pragma once

#include <array>
#include <string_view>
#include <vector>

#include <ftxui/screen/color.hpp>

#include "syntax/highlighter.hpp"

// the colors: of the syntax groups (changed one by one in the View tab), and
// the themes that set them all at once together with the colors of the rest
// of the editor. Uses only ftxui::Color (a plain color value), no components.

struct NamedColor {
    const char* name;
    ftxui::Color color;
};

// colors the user can pick from in the View tab. Index 0 ("Default") means
// "leave the cell as it is".
const std::vector<NamedColor>& color_palette();

int palette_index(std::string_view name);

struct GroupSetting {
    const char* label;   // shown in the View tab: "Keyword"
    const char* key;     // in the config file: "color.keyword"
    int color_index;     // into color_palette()
};

inline constexpr int group_count = static_cast<int>(SyntaxGroup::Count);

// label + current color of every group.
std::array<GroupSetting, group_count>& group_settings();

// the color a byte of syntax group `group` is drawn in (Color::Default: as is).
ftxui::Color group_color(unsigned char group);

struct Theme {
    const char* name;
    std::array<const char*, group_count> syntax;  // a color_palette() name per group
    ftxui::Color accent;      // the cursor's line number, the shown tab
    ftxui::Color open_file;   // the open file in the explorer
    ftxui::Color selection;   // background of selected text
    ftxui::Color shade;       // background of the cursor's row, when shaded
    ftxui::Color status_background;
    ftxui::Color status_foreground;
};

// colors of search matches: every match, and the one the cursor is on.
inline const ftxui::Color search_match_background = ftxui::Color::Yellow3;
inline const ftxui::Color search_current_background = ftxui::Color::Orange1;
inline const ftxui::Color search_match_foreground = ftxui::Color::Black;

// the themes of the View tab; the first one is the default.
const std::vector<Theme>& themes();
// the theme chosen in the View tab (settings.theme).
const Theme& current_theme();
// gives every syntax group the color of theme `index` (the choice in the View tab).
void apply_theme(int index);

// the theme and every syntax color in the config file (editor/config.hpp).
void add_syntax_config_entries();
