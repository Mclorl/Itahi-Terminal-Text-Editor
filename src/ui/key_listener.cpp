#include "ui/key_listener.hpp"

#include <string>

#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "ui/ui_state.hpp"

namespace {

    // what the terminal answers when FTXUI asks it something at start (its
    // name, what it can do, where the cursor is): not a key the user pressed,
    // so it must not clear a message shown on the first frame.
    bool is_terminal_reply(const ftxui::Event& event) {
        if (event.is_cursor_position() || event.is_cursor_shape()) {
            return true;
        }
        const std::string& input = event.input();
        if (input.size() < 3 || input[0] != '\x1B') {
            return false;
        }
        return input[1] == 'P' || input[1] == ']' || input[1] == '_' ||
               (input[1] == '[' && (input[2] == '?' || input[2] == '>'));
    }

}  // namespace

ftxui::Component key_listener_function(ftxui::ScreenInteractive &app_screen, ftxui::Component component,
                                       std::function<bool()> copy) {
    // the app handles Ctrl+C itself, and Ctrl+Z is undo instead of "suspend".
    app_screen.ForceHandleCtrlC(false);
    app_screen.ForceHandleCtrlZ(false);

    return ftxui::CatchEvent(component, [&app_screen, copy](ftxui::Event event) {
        // the result of the last command, and an error shown while the
        // command line is closed, stay until the next key press. (Mouse moves
        // and the redraws posted by the app don't count. The command line
        // clears its own error when it closes.)
        if (!event.is_mouse() && event != ftxui::Event::Custom && !is_terminal_reply(event)) {
            command_output.clear();
            if (mode != Mode::Command) {
                command_error.clear();
            }
        }

        // Ctrl+C copies the selected text; with nothing selected it quits,
        // but never with unsaved work (like ">q").
        if (event == ftxui::Event::CtrlC) {
            if (mode != Mode::Command && copy && copy()) {
                return true;
            }
            if (const Document* unsaved = workspace.first_unsaved()) {
                command_error = "Unsaved changes in " + unsaved->path() + ": >w saves, >q! quits without saving";
                return true;
            }
            // the same way as >q, so it counts as a quit that was asked for.
            if (hooks.quit) {
                hooks.quit();
            } else {
                app_screen.ExitLoopClosure()();
            }
            return true;
        }
        return false;
    });
}
