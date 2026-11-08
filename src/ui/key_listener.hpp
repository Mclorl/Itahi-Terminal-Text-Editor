#pragma once

#include <functional>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

// the outermost key handler: Ctrl+C copies the selection (`copy`, false when
// nothing is selected) or else quits (refused while files are unsaved), and
// the last command message is cleared on the next key press.
ftxui::Component key_listener_function(ftxui::ScreenInteractive &app_screen, ftxui::Component component,
                                       std::function<bool()> copy);
