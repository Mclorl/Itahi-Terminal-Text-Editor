#pragma once


// #include <termios.h>

// extern struct termios orig_termios;

// void disableRawMode(void);
// void enableRawMode(void);


#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

ftxui::Component key_listener_function(ftxui::ScreenInteractive &screen, ftxui::Component component);