#pragma once

#include <ftxui/component/component.hpp>

// a tab for every open file, above the editor: a click shows it, a click on
// its "x" (or a middle click) closes it. The shown file's tab is highlighted,
// an unsaved one has a '*'. Alt+Left / Alt+Right, >bn / >bp and >b name
// switch between them from the keyboard. Turned off in the View tab.
ftxui::Component make_tab_bar();
