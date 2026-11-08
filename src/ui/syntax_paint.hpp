#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>

// decorator for the editor's Input element: once the Input has drawn the text,
// recolor every cell according to the syntax of `text` (the file at `path`).
// The Input itself is untouched, so the cursor, mouse and editing keep working.
ftxui::Decorator syntax_highlight(const std::string& text, const std::string& path);
