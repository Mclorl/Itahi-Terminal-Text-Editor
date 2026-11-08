#include "platform/terminal.hpp"

#include <cstdlib>
#include <string>

bool terminal_shows_underline_color() {
    const char* term_value = std::getenv("TERM");
    const std::string term = term_value ? term_value : "";
    if (term.empty() || term == "dumb" || term == "linux" || term.rfind("vt", 0) == 0 || term == "cons25") {
        return false;  // consoles and old terminals
    }
    // GNU screen drops the underline color; tmux passes it on (and also uses
    // "screen" names for TERM), so only screen without tmux counts.
    if (term.rfind("screen", 0) == 0 && !std::getenv("TMUX")) {
        return false;
    }
    // every common terminal emulator of the last years shows it: VTE (GNOME
    // Terminal, Ptyxis, ...), Konsole, kitty, WezTerm, foot, Alacritty,
    // iTerm2, Windows Terminal, ...
    return true;
}
