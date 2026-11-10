#pragma once

#include <ftxui/component/component.hpp>

// the list of keys and commands shown in the Help tab (F1, >help). The
// commands come from the command table itself (editor/commands.hpp), so a new
// command is listed without anything to remember here.
ftxui::Component make_help_view();
