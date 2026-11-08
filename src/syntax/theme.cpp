#include "syntax/theme.hpp"

#include <algorithm>
#include <string>

#include "editor/config.hpp"
#include "editor/settings.hpp"

// colors the user can pick from in the View tab. Index 0 ("Default") means
// "leave the cell as it is".
const std::vector<NamedColor>& color_palette() {
    static const std::vector<NamedColor> palette = {
        {"Default",       ftxui::Color::Default},
        {"White",         ftxui::Color::White},
        {"Light Gray",    ftxui::Color::GrayLight},
        {"Dark Gray",     ftxui::Color::GrayDark},
        {"Red",           ftxui::Color::Red},
        {"Light Red",     ftxui::Color::RedLight},
        {"Green",         ftxui::Color::Green},
        {"Light Green",   ftxui::Color::GreenLight},
        {"Yellow",        ftxui::Color::Yellow},
        {"Light Yellow",  ftxui::Color::YellowLight},
        {"Blue",          ftxui::Color::Blue},
        {"Light Blue",    ftxui::Color::BlueLight},
        {"Magenta",       ftxui::Color::Magenta},
        {"Light Magenta", ftxui::Color::MagentaLight},
        {"Cyan",          ftxui::Color::Cyan},
        {"Light Cyan",    ftxui::Color::CyanLight},
        {"Orange",        ftxui::Color::Orange1},
        {"Gold",          ftxui::Color::Gold1},
        {"Pink",          ftxui::Color::Pink1},
        {"Light Pink",    ftxui::Color::LightPink1},
        {"Salmon",        ftxui::Color::Salmon1},
        {"Violet",        ftxui::Color::Violet},
        {"Purple",        ftxui::Color::Purple},
        {"Sky Blue",      ftxui::Color::SkyBlue1},
        {"Spring Green",  ftxui::Color::SpringGreen1},
        {"Aquamarine",    ftxui::Color::Aquamarine1},
        {"Khaki",         ftxui::Color::Khaki1},
    };
    return palette;
}

int palette_index(std::string_view name) {
    const auto& palette = color_palette();
    for (std::size_t i = 0; i < palette.size(); ++i) {
        if (name == palette[i].name) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

// the themes. Each lists a color per syntax group, in SyntaxGroup order:
// None, Keyword, Preprocessor, Type, Function, Variable, Property, String,
// Escape, Number, Constant, Comment, Operator, Punctuation, Tag, Heading,
// Diff Added, Diff Removed.
const std::vector<Theme>& themes() {
    static const std::vector<Theme> list = {
        // the original colors, after vim's default dark colorscheme
        // (Statement yellow, Type green, Constant pink, ...).
        {"Itahi",
         {"Default", "Yellow", "Light Magenta", "Light Green", "Light Cyan", "Default", "Default", "Light Pink",
          "Orange", "Light Pink", "Light Pink", "Sky Blue", "Default", "Default", "Cyan", "Light Magenta",
          "Light Green", "Light Red"},
         ftxui::Color::Yellow, ftxui::Color::Orange1, ftxui::Color::RGB(38, 79, 120), ftxui::Color::RGB(48, 48, 48),
         ftxui::Color::GrayDark, ftxui::Color::White},
        // warm: oranges and golds.
        {"Ember",
         {"Default", "Orange", "Pink", "Gold", "Light Yellow", "Default", "Khaki", "Light Green",
          "Salmon", "Light Pink", "Salmon", "Dark Gray", "Default", "Default", "Orange", "Gold",
          "Light Green", "Light Red"},
         ftxui::Color::Orange1, ftxui::Color::Gold1, ftxui::Color::RGB(94, 58, 30), ftxui::Color::RGB(52, 40, 30),
         ftxui::Color::RGB(120, 62, 22), ftxui::Color::White},
        // cool: blues and cyans.
        {"Glacier",
         {"Default", "Light Blue", "Violet", "Aquamarine", "Sky Blue", "Default", "Light Cyan", "Spring Green",
          "Light Magenta", "Light Magenta", "Violet", "Dark Gray", "Default", "Default", "Light Blue", "Sky Blue",
          "Spring Green", "Light Red"},
         ftxui::Color::SkyBlue1, ftxui::Color::CyanLight, ftxui::Color::RGB(30, 60, 100), ftxui::Color::RGB(30, 40, 56),
         ftxui::Color::RGB(30, 70, 110), ftxui::Color::White},
        // greens and earth colors.
        {"Grove",
         {"Default", "Light Green", "Khaki", "Spring Green", "Light Yellow", "Default", "Default", "Gold",
          "Orange", "Khaki", "Gold", "Dark Gray", "Default", "Default", "Aquamarine", "Light Green",
          "Light Green", "Light Red"},
         ftxui::Color::GreenLight, ftxui::Color::SpringGreen1, ftxui::Color::RGB(40, 80, 40), ftxui::Color::RGB(36, 46, 36),
         ftxui::Color::RGB(40, 90, 50), ftxui::Color::White},
        // no syntax colors: only comments and strings stand out.
        {"Mono",
         {"Default", "White", "Default", "Default", "Default", "Default", "Default", "Light Gray",
          "Default", "Default", "Default", "Dark Gray", "Default", "Default", "Default", "White",
          "Default", "Default"},
         ftxui::Color::White, ftxui::Color::White, ftxui::Color::RGB(80, 80, 80), ftxui::Color::RGB(45, 45, 45),
         ftxui::Color::GrayLight, ftxui::Color::Black},
    };
    return list;
}

const Theme& current_theme() {
    const auto& list = themes();
    return list[static_cast<std::size_t>(std::clamp(settings.theme, 0, static_cast<int>(list.size()) - 1))];
}

// label + current color of every group (the first theme's colors to start with).
std::array<GroupSetting, group_count>& group_settings() {
    static std::array<GroupSetting, group_count> groups = [] {
        std::array<GroupSetting, group_count> g{};
        auto set = [&g](SyntaxGroup group, const char* label, const char* key) {
            const int i = static_cast<int>(group);
            g[i] = {label, key, palette_index(themes()[0].syntax[static_cast<std::size_t>(i)])};
        };
        set(SyntaxGroup::None,         "None",         "color.none");
        set(SyntaxGroup::Keyword,      "Keyword",      "color.keyword");
        set(SyntaxGroup::Preprocessor, "Preprocessor", "color.preprocessor");
        set(SyntaxGroup::Type,         "Type",         "color.type");
        set(SyntaxGroup::Function,     "Function",     "color.function");
        set(SyntaxGroup::Variable,     "Variable",     "color.variable");
        set(SyntaxGroup::Property,     "Property",     "color.property");
        set(SyntaxGroup::String,       "String",       "color.string");
        set(SyntaxGroup::Special,      "Escape",       "color.escape");
        set(SyntaxGroup::Number,       "Number",       "color.number");
        set(SyntaxGroup::Constant,     "Constant",     "color.constant");
        set(SyntaxGroup::Comment,      "Comment",      "color.comment");
        set(SyntaxGroup::Operator,     "Operator",     "color.operator");
        set(SyntaxGroup::Punctuation,  "Punctuation",  "color.punctuation");
        set(SyntaxGroup::Tag,          "Tag",          "color.tag");
        set(SyntaxGroup::Heading,      "Heading",      "color.heading");
        set(SyntaxGroup::DiffAdded,    "Diff Added",   "color.diff_added");
        set(SyntaxGroup::DiffRemoved,  "Diff Removed", "color.diff_removed");
        return g;
    }();
    return groups;
}

ftxui::Color group_color(unsigned char group) {
    if (group == 0 || group >= group_count) {
        return ftxui::Color::Default;
    }
    const int index = group_settings()[group].color_index;
    return index == 0 ? ftxui::Color::Default : color_palette()[static_cast<std::size_t>(index)].color;
}

void apply_theme(int index) {
    const auto& list = themes();
    const Theme& theme = list[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(list.size()) - 1))];
    auto& groups = group_settings();
    for (int group = 0; group < group_count; ++group) {
        groups[static_cast<std::size_t>(group)].color_index = palette_index(theme.syntax[static_cast<std::size_t>(group)]);
    }
}

void add_syntax_config_entries() {
    // the theme first: the colors after it may change some of its colors.
    add_config_entry({"theme",
                      [] { return std::string(current_theme().name); },
                      [](const std::string& name) {
                          const auto& list = themes();
                          for (std::size_t i = 0; i < list.size(); ++i) {
                              if (name == list[i].name) {
                                  // its colors; the color.* lines after it can change some.
                                  settings.theme = static_cast<int>(i);
                                  apply_theme(settings.theme);
                                  return true;
                              }
                          }
                          return false;
                      }});
    for (int group = 1; group < group_count; ++group) {
        add_config_entry({group_settings()[static_cast<std::size_t>(group)].key,
                          [group] {
                              return std::string(color_palette()[static_cast<std::size_t>(group_settings()[static_cast<std::size_t>(group)].color_index)].name);
                          },
                          [group](const std::string& name) {
                              const auto& palette = color_palette();
                              const auto found = std::find_if(palette.begin(), palette.end(), [&](const NamedColor& c) { return name == c.name; });
                              if (found == palette.end()) {
                                  return false;
                              }
                              group_settings()[static_cast<std::size_t>(group)].color_index = static_cast<int>(found - palette.begin());
                              return true;
                          }});
    }
}
