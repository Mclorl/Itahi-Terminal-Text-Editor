#include "../library/terminal_handling.hpp"

// #include <iostream>
// #include <termios.h>
// #include <unistd.h>

// struct orig_termios{


// };

// void disableRawMode(void) {
//     tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
// }

// void enableRawMode(void) {
//     tcgetattr(STDIN_FILENO, &orig_termios);
//     std::atexit(disableRawMode); // terminal resets when program exits

//     struct termios raw = orig_termios;
//     // turn off canonical mode, echo and signals.
//     raw.c_lflag &= ~(ECHO | ICANON | ISIG);

//     tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
// }

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "../library/global_variables.hpp"

ftxui::Component key_listener_function(ftxui::ScreenInteractive &screen, ftxui::Component component) {

    screen.ForceHandleCtrlC(false);

    return ftxui::CatchEvent(component, [&screen](ftxui::Event event) {
        // Ctrl+C arrives as Event::CtrlC (input "\x03").
        if (event == ftxui::Event::CtrlC) {
            
            current_events = "are you sure you want to exit?";
            screen.ExitLoopClosure()();
            return true;
        }

        // the result of the last command, and an error shown while the
        // command line is closed, stay until the next key press. (Mouse moves
        // and the redraws posted by the app don't count. The command line
        // clears its own error when it closes.)
        if (!event.is_mouse() && event != ftxui::Event::Custom) {
            command_output.clear();
            if (!is_command_mode) {
                command_error.clear();
            }
        }

        // if (event == ftxui::Event::Special("\x1B")) {
        //     current_events = "you are pressing ESC";
        //     return true;
        // }
        return false;
    });
}