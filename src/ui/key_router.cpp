#include "ui/key_router.hpp"

#include <memory>
#include <string>

#include <ftxui/component/event.hpp>

#include "editor/commands.hpp"
#include "editor/mode.hpp"
#include "editor/paths.hpp"
#include "editor/state.hpp"
#include "editor/undo_history.hpp"
#include "editor/workspace.hpp"
#include "ui/editor_view.hpp"

namespace {

    using ftxui::Event;

    // Ctrl+H and Ctrl+J only arrive as their own events thanks to
    // patches/ftxui-ctrl-h-ctrl-j.patch (stock FTXUI merges them with
    // Backspace and Enter). Ctrl+J comes in as a CSI-u style sequence.
    const Event ctrl_j = Event::Special("\x1B[106;5u");

    const Event paste_start = Event::Special("\x1B[200~");
    const Event paste_end = Event::Special("\x1B[201~");
    const Event alt_left = Event::Special("\x1B[1;3D");
    const Event alt_right = Event::Special("\x1B[1;3C");
    const Event ctrl_page_up = Event::Special("\x1B[5;5~");
    const Event ctrl_page_down = Event::Special("\x1B[6;5~");

    void show(const CommandResult& result) {
        (result.ok ? command_output : command_error) = result.message;
    }

    struct RouterState {
        bool in_paste = false;
        std::string pasted;       // the text of the paste, inserted when it ends
        bool came_from_explorer = false;
    };

}  // namespace

ftxui::Component make_key_router(ftxui::Component child, KeyRouterParts parts) {
    auto state = std::make_shared<RouterState>();

    return ftxui::CatchEvent(std::move(child), [state, parts](Event event) {
        // bracketed paste: pasted text is data, never a command. In Edit mode
        // it goes into the text in one piece (one undo step) when it ends.
        if (event == paste_start) {
            state->in_paste = true;
            state->pasted.clear();
            return true;
        }
        if (event == paste_end) {
            state->in_paste = false;
            if (mode == Mode::Edit && parts.editor->Focused()) {
                parts.editor->insert_pasted(state->pasted);
            } else if (mode == Mode::Edit) {
                command_error = "Pasted text goes into the editor: click it (or Ctrl+Right) first";
            }
            state->pasted.clear();
            return true;
        }
        if (state->in_paste) {
            if (event.is_character()) {
                state->pasted += event.character();
            } else if (event == Event::Return || event == ctrl_j) {
                state->pasted += '\n';
            } else if (event == Event::Tab) {
                state->pasted += '\t';
            }
            return true;
        }

        // Esc always leaves Edit mode, wherever the focus is.
        if (mode == Mode::Edit && event == Event::Escape) {
            set_normal_mode();
            return true;
        }

        // familiar shortcuts, in Normal and Edit mode.
        if (mode != Mode::Command) {
            if (event == Event::CtrlS) { show(save_active_file()); return true; }
            if (event == Event::CtrlZ) { show(undo_edit()); return true; }
            if (event == Event::CtrlY) { show(redo_edit()); return true; }
            // the next / previous open file (tab).
            if (event == alt_right || event == ctrl_page_down) { show(switch_file(+1)); return true; }
            if (event == alt_left || event == ctrl_page_up) { show(switch_file(-1)); return true; }
            // F5: read the folders and Git's status again (files changed by other programs).
            if (event == Event::F5) { show(run_command("refresh")); return true; }
        }

        // Edit mode, but the focus is outside the text: keys that would be
        // commands or shortcuts in Normal mode do nothing here, so say why.
        // (Arrows, Enter and Tab still move around menus and the explorer.)
        if (mode == Mode::Edit && !parts.editor->Focused()) {
            const bool command_key = event.is_character() ||
                event == Event::ArrowLeftCtrl || event == Event::ArrowRightCtrl ||
                event == Event::ArrowUpCtrl || event == Event::ArrowDownCtrl ||
                event == Event::CtrlH || event == Event::CtrlK || event == Event::CtrlL ||
                event == Event::CtrlN || event == ctrl_j;
            if (command_key) {
                command_error = "You are in Edit mode: commands and key shortcuts only work in Normal mode. Press Esc to switch.";
                return true;
            }
        }

        if (mode != Mode::Normal) {
            return false;
        }

        // Ctrl+N: New File, in the active folder.
        if (event == Event::CtrlN) {
            parts.start_command_line("e " + active_folder_for_command());
            return true;
        }

        // '>' opens the command line from anywhere (editor, explorer, menus).
        if (event == Event::Character('>')) {
            parts.start_command_line("");
            return true;
        }
        // '/' and '?' search down / up in the open file (like vim).
        if ((event == Event::Character('/') || event == Event::Character('?')) && workspace.active()) {
            parts.start_command_line(event.character());
            return true;
        }

        // Ctrl+Arrow or Ctrl+H/J/K/L move the focus between the top bar, the
        // explorer and the main content. Plain arrows keep moving inside the
        // focused panel.
        const bool go_left  = event == Event::ArrowLeftCtrl  || event == Event::CtrlH;
        const bool go_right = event == Event::ArrowRightCtrl || event == Event::CtrlL;
        const bool go_up    = event == Event::ArrowUpCtrl    || event == Event::CtrlK;
        const bool go_down  = event == Event::ArrowDownCtrl  || event == ctrl_j;

        if (!go_left && !go_right && !go_up && !go_down) {
            return false;
        }

        const bool in_top_bar  = parts.top_bar->Focused();
        const bool in_explorer = parts.explorer && parts.explorer->Focused();

        auto focus_explorer = [&] {
            if (parts.explorer && parts.explorer->Focusable()) {
                parts.explorer->TakeFocus();
            }
        };

        if (go_up && !in_top_bar) {
            state->came_from_explorer = in_explorer;
            parts.top_bar->TakeFocus();
        } else if (go_down && in_top_bar) {
            if (state->came_from_explorer) {
                focus_explorer();
            } else {
                parts.main_content->TakeFocus();
            }
        } else if (go_left && !in_top_bar && !in_explorer) {
            focus_explorer();
        } else if (go_right && in_explorer) {
            parts.main_content->TakeFocus();
        }

        // always consumed in normal mode, even when there is nowhere to go,
        // so the shortcut never falls through to the focused component.
        return true;
    });
}
