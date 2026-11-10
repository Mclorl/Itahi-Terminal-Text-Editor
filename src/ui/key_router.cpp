#include "ui/key_router.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>

#include "editor/commands.hpp"
#include "editor/mode.hpp"
#include "editor/paths.hpp"
#include "editor/state.hpp"
#include "editor/undo_history.hpp"
#include "editor/workspace.hpp"
#include "ui/editor_view.hpp"
#include "ui/ui_state.hpp"
#include "ui/window_grid.hpp"

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
    const Event alt_up = Event::Special("\x1B[1;3A");
    const Event alt_down = Event::Special("\x1B[1;3B");
    const Event ctrl_page_up = Event::Special("\x1B[5;5~");
    const Event ctrl_page_down = Event::Special("\x1B[6;5~");
    const Event ctrl_backslash = Event::Special("\x1C");
    const Event shift_f6 = Event::Special("\x1B[17;2~");
    const Event shift_left = Event::Special("\x1B[1;2D");
    const Event shift_right = Event::Special("\x1B[1;2C");
    const Event shift_up = Event::Special("\x1B[1;2A");
    const Event shift_down = Event::Special("\x1B[1;2B");
    const Event alt_shift_left = Event::Special("\x1B[1;4D");
    const Event alt_shift_right = Event::Special("\x1B[1;4C");
    const Event alt_shift_up = Event::Special("\x1B[1;4A");
    const Event alt_shift_down = Event::Special("\x1B[1;4B");

    const char* const window_keys_hint =
        "Window:  arrows go  Alt+arrows move  v split right  s split down  c close  o only  w next  = same size";

    void show(const CommandResult& result) {
        (result.ok ? command_output : command_error) = result.message;
    }

    // a key and the command it runs.
    struct Shortcut {
        Event key;
        const char* command;
    };

    // the first shortcut for `event`, run as its command. False: not one of them.
    bool run_shortcut(const std::vector<Shortcut>& shortcuts, const Event& event) {
        for (const Shortcut& shortcut : shortcuts) {
            if (event == shortcut.key) {
                show(run_command(shortcut.command));
                return true;
            }
        }
        return false;
    }

    struct RouterState {
        bool in_paste = false;
        std::string pasted;       // the text of the paste, inserted when it ends
        bool came_from_explorer = false;
        bool window_key = false;  // w was pressed (Normal mode): the next key is for the windows
    };

    // bracketed paste: pasted text is data, never a command. In Edit mode it
    // goes into the text in one piece (one undo step) when it ends.
    bool handle_paste(RouterState& state, const KeyRouterParts& parts, const Event& event) {
        if (event == paste_start) {
            state.in_paste = true;
            state.pasted.clear();
            return true;
        }
        if (event == paste_end) {
            state.in_paste = false;
            EditorView& editor = parts.windows->active_editor();
            if (mode == Mode::Edit && editor.Focused()) {
                editor.insert_pasted(state.pasted);
            } else if (mode == Mode::Edit) {
                command_error = "Pasted text goes into the editor: click it (or Ctrl+Right) first";
            }
            state.pasted.clear();
            return true;
        }
        if (!state.in_paste) {
            return false;
        }
        if (event.is_character()) {
            state.pasted += event.character();
        } else if (event == Event::Return || event == ctrl_j) {
            state.pasted += '\n';
        } else if (event == Event::Tab) {
            state.pasted += '\t';
        }
        return true;
    }

    // the key after w (Normal mode), like vim's Ctrl+W window commands.
    void handle_window_key(const KeyRouterParts& parts, const Event& event) {
        static const std::vector<Shortcut> keys = {
            {Event::ArrowLeft, "win left"},   {Event::Character('h'), "win left"},
            {Event::ArrowRight, "win right"}, {Event::Character('l'), "win right"},
            {Event::ArrowUp, "win up"},       {Event::Character('k'), "win up"},
            {Event::ArrowDown, "win down"},   {Event::Character('j'), "win down"},
            {shift_left, "wmove left"},       {Event::Character('H'), "wmove left"},
            {shift_right, "wmove right"},     {Event::Character('L'), "wmove right"},
            {shift_up, "wmove up"},           {Event::Character('K'), "wmove up"},
            {shift_down, "wmove down"},       {Event::Character('J'), "wmove down"},
            {Event::Character('v'), "vsplit"}, {Event::Character('s'), "split"},
            {Event::Character('c'), "close"},  {Event::Character('q'), "close"},
            {Event::Character('o'), "only"},
            {Event::Character('w'), "win next"},
            {Event::Character('W'), "win prev"}, {Event::Character('p'), "win prev"},
            {Event::Character('1'), "win 1"}, {Event::Character('2'), "win 2"},
            {Event::Character('3'), "win 3"}, {Event::Character('4'), "win 4"},
            // Konsole keeps Shift+Left/Right (switch tab) for itself: Alt+arrows
            // after w move the window too.
            {alt_left, "wmove left"}, {alt_right, "wmove right"},
            {alt_up, "wmove up"},     {alt_down, "wmove down"},
        };
        if (event == Event::Escape || run_shortcut(keys, event)) {
            return;
        }
        if (event == Event::Character('=')) {
            parts.windows->equalize();
        } else if (event == Event::Character('>') || event == Event::Character('<')) {
            parts.windows->resize_active(event == Event::Character('>') ? 4 : -4, 0);
            command_output = window_keys_hint;
        } else if (event == Event::Character('+') || event == Event::Character('-')) {
            parts.windows->resize_active(0, event == Event::Character('+') ? 2 : -2);
        } else {
            command_error = std::string("Not a window key. ") + window_keys_hint;
        }
    }

    // F6 / Shift+F6: the next part of the editor, in a ring: the explorer,
    // then every window.
    void focus_next_part(const KeyRouterParts& parts, int step) {
        const bool has_explorer = parts.explorer && parts.explorer->Focusable();
        const int windows = static_cast<int>(workspace.window_count());
        const int count = windows + (has_explorer ? 1 : 0);
        // 0: the explorer; 1..: the windows.
        int at = has_explorer && parts.explorer->Focused() ? 0 : workspace.window_number(workspace.active_window());
        at = ((at + step) % (windows + 1) + (windows + 1)) % (windows + 1);
        if (at == 0 && !has_explorer) {
            at = step > 0 ? 1 : windows;
        }
        if (count < 2) {
            return;
        }
        if (at == 0) {
            parts.explorer->TakeFocus();
        } else {
            workspace.focus_window(workspace.window_by_number(at));
            call(hooks.show_editor);
        }
    }

    // Ctrl+Arrow or Ctrl+H/J/K/L: to what is on that side. Between the
    // windows first; past the last window on a side, the explorer (left) or
    // the menu bar (up). Plain arrows keep moving inside the focused part.
    void move_focus(RouterState& state, const KeyRouterParts& parts, Side side) {
        const bool in_top_bar = parts.top_bar->Focused();
        const bool in_explorer = parts.explorer && parts.explorer->Focused();
        const bool in_windows = !in_top_bar && !in_explorer && status == 1;
        const auto focus_explorer = [&] {
            if (parts.explorer && parts.explorer->Focusable()) {
                parts.explorer->TakeFocus();
            }
        };

        if (in_windows && workspace.focus_side(side)) {
            return;  // another window
        }
        switch (side) {
            case Side::Up:
                if (!in_top_bar) {
                    state.came_from_explorer = in_explorer;
                    parts.top_bar->TakeFocus();
                }
                break;
            case Side::Down:
                if (in_top_bar) {
                    if (state.came_from_explorer) {
                        focus_explorer();
                    } else {
                        parts.main_content->TakeFocus();
                    }
                }
                break;
            case Side::Left:
                if (!in_top_bar && !in_explorer) {
                    focus_explorer();
                }
                break;
            case Side::Right:
                if (in_explorer) {
                    parts.main_content->TakeFocus();
                }
                break;
        }
    }

}  // namespace

ftxui::Component make_key_router(ftxui::Component child, KeyRouterParts parts) {
    auto state = std::make_shared<RouterState>();

    // familiar shortcuts, in Normal and Edit mode.
    static const std::vector<Shortcut> shortcuts = {
        {Event::CtrlS, "w"},
        {Event::CtrlZ, "undo"},
        {Event::CtrlY, "redo"},
        {alt_right, "bn"}, {ctrl_page_down, "bn"},  // the next / previous open file (tab)
        {alt_left, "bp"},  {ctrl_page_up, "bp"},
        {Event::F5, "refresh"},  // files changed by other programs
        {Event::F1, "help"},
        {ctrl_backslash, "vsplit"},
        {alt_shift_left, "wmove left"}, {alt_shift_right, "wmove right"},
        {alt_shift_up, "wmove up"},     {alt_shift_down, "wmove down"},
    };

    return ftxui::CatchEvent(std::move(child), [state, parts](Event event) {
        if (handle_paste(*state, parts, event)) {
            return true;
        }

        // the key after w belongs to the windows, whatever it is.
        if (state->window_key && !event.is_mouse() && event != Event::Custom) {
            state->window_key = false;
            handle_window_key(parts, event);
            return true;
        }

        // Esc always leaves Edit mode, wherever the focus is.
        if (mode == Mode::Edit && event == Event::Escape) {
            set_normal_mode();
            return true;
        }

        if (mode != Mode::Command) {
            if (event == Event::F6 || event == shift_f6) {
                focus_next_part(parts, event == Event::F6 ? 1 : -1);
                return true;
            }
            if (run_shortcut(shortcuts, event)) {
                return true;
            }
        }

        // Edit mode, but the focus is outside the text: keys that would be
        // commands or shortcuts in Normal mode do nothing here, so say why.
        // (Arrows, Enter and Tab still move around menus and the explorer.)
        if (mode == Mode::Edit && !parts.windows->active_editor().Focused()) {
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

        // 'w': the next key is for the windows (w v, w s, w arrows, ...).
        if (event == Event::Character('w')) {
            state->window_key = true;
            command_output = window_keys_hint;
            return true;
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

        std::optional<Side> side;
        if (event == Event::ArrowLeftCtrl || event == Event::CtrlH) side = Side::Left;
        if (event == Event::ArrowRightCtrl || event == Event::CtrlL) side = Side::Right;
        if (event == Event::ArrowUpCtrl || event == Event::CtrlK) side = Side::Up;
        if (event == Event::ArrowDownCtrl || event == ctrl_j) side = Side::Down;
        if (!side) {
            return false;
        }
        // always consumed in normal mode, even when there is nowhere to go,
        // so the shortcut never falls through to the focused component.
        move_focus(*state, parts, *side);
        return true;
    });
}
