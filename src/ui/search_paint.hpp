#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

// colors of search matches: every match, and the one the cursor is on.
inline const ftxui::Color search_match_background = ftxui::Color::Yellow3;
inline const ftxui::Color search_current_background = ftxui::Color::Orange1;
inline const ftxui::Color search_match_foreground = ftxui::Color::Black;

// decorator for the editor's Input element: once the Input has drawn `text`,
// paint the background of every search match (see editor/search.hpp).
ftxui::Decorator search_highlight(const std::string& text);
