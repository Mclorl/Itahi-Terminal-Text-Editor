#include "syntax/theme.hpp"

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

// label + current color of every group. Defaults follow vim's default
// dark colorscheme (Statement yellow, Type green, Constant pink, ...).
std::array<GroupSetting, group_count>& group_settings() {
    static std::array<GroupSetting, group_count> settings = [] {
        std::array<GroupSetting, group_count> s{};
        auto set = [&s](SyntaxGroup group, const char* label, const char* color) {
            s[static_cast<int>(group)] = {label, palette_index(color)};
        };
        set(SyntaxGroup::None,         "None",         "Default");
        set(SyntaxGroup::Keyword,      "Keyword",      "Yellow");
        set(SyntaxGroup::Preprocessor, "Preprocessor", "Light Magenta");
        set(SyntaxGroup::Type,         "Type",         "Light Green");
        set(SyntaxGroup::Function,     "Function",     "Light Cyan");
        set(SyntaxGroup::Variable,     "Variable",     "Default");
        set(SyntaxGroup::Property,     "Property",     "Default");
        set(SyntaxGroup::String,       "String",       "Light Pink");
        set(SyntaxGroup::Special,      "Escape",       "Orange");
        set(SyntaxGroup::Number,       "Number",       "Light Pink");
        set(SyntaxGroup::Constant,     "Constant",     "Light Pink");
        set(SyntaxGroup::Comment,      "Comment",      "Sky Blue");
        set(SyntaxGroup::Operator,     "Operator",     "Default");
        set(SyntaxGroup::Punctuation,  "Punctuation",  "Default");
        set(SyntaxGroup::Tag,          "Tag",          "Cyan");
        set(SyntaxGroup::Heading,      "Heading",      "Light Magenta");
        set(SyntaxGroup::DiffAdded,    "Diff Added",   "Light Green");
        set(SyntaxGroup::DiffRemoved,  "Diff Removed", "Light Red");
        return s;
    }();
    return settings;
}
