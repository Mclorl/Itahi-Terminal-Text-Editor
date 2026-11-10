#pragma once

#include <functional>
#include <memory>
#include <string>

#include <ftxui/component/component.hpp>

class WindowGrid;

// the keys that work wherever the focus is, before the focused component
// sees them: bracketed paste, Esc out of Edit mode, Ctrl+S / Ctrl+Z / Ctrl+Y,
// switching tabs (Alt+Left / Alt+Right, Ctrl+Page Up / Page Down), F1, F5,
// the windows (w then a key in Normal mode, Ctrl+\, F6, Alt+Shift+arrows), and in
// Normal mode Ctrl+N, '>', '/', '?' and moving between the windows and the
// panels (Ctrl + arrows or Ctrl + H / J / K / L).
struct KeyRouterParts {
    ftxui::Component top_bar;
    ftxui::Component explorer;      // nullptr when one file was opened (no explorer)
    ftxui::Component main_content;  // the windows and the menus
    std::shared_ptr<WindowGrid> windows;
    std::function<void(const std::string&)> start_command_line;
};

ftxui::Component make_key_router(ftxui::Component child, KeyRouterParts parts);
