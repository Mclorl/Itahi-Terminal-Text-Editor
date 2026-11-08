#pragma once

#include <ftxui/dom/elements.hpp>

// the row under the editor: the mode, the open file (with '*' when unsaved)
// and its Git branch on the left; the selection, the cursor's line and
// column, the language, the indentation, the encoding and the line breaks on
// the right. Turned off in the View tab.
ftxui::Element status_bar_element();
