#pragma once

#include <functional>
#include <memory>
#include <string>

#include <ftxui/component/component.hpp>

class EditorView;

// the keys that work wherever the focus is, before the focused component
// sees them: bracketed paste, Esc out of Edit mode, Ctrl+S / Ctrl+Z / Ctrl+Y,
// switching tabs (Alt+Left / Alt+Right, Ctrl+Page Up / Page Down), F5, and in
// Normal mode Ctrl+N, '>', '/', '?' and moving between the panels (Ctrl +
// arrows or Ctrl + H / J / K / L).
struct KeyRouterParts {
    ftxui::Component top_bar;
    ftxui::Component explorer;      // nullptr when one file was opened (no explorer)
    ftxui::Component main_content;  // the editor and the menus
    std::shared_ptr<EditorView> editor;
    std::function<void(const std::string&)> start_command_line;
};

ftxui::Component make_key_router(ftxui::Component child, KeyRouterParts parts);
