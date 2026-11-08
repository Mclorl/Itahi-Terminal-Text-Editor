#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

// Ctrl+C (refused while files are unsaved) and clearing the last command
// message on the next key press.
ftxui::Component key_listener_function(ftxui::ScreenInteractive &app_screen, ftxui::Component component);
