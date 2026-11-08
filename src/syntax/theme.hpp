#pragma once

#include <array>
#include <string_view>
#include <vector>

#include <ftxui/screen/color.hpp>

#include "syntax/highlighter.hpp"

// the colors of the syntax groups, changed in the View tab. Uses only
// ftxui::Color (a plain color value), no components.

struct NamedColor {
    const char* name;
    ftxui::Color color;
};

// colors the user can pick from in the View tab. Index 0 ("Default") means
// "leave the cell as it is".
const std::vector<NamedColor>& color_palette();

int palette_index(std::string_view name);

struct GroupSetting {
    const char* label;
    int color_index; // into color_palette()
};

inline constexpr int group_count = static_cast<int>(SyntaxGroup::Count);

// label + current color of every group.
std::array<GroupSetting, group_count>& group_settings();
