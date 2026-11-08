#pragma once

// what the terminal Itahi runs in can show, guessed from its environment
// (TERM, TMUX), before anything is drawn.

// false for terminals known to ignore a separate underline color (SGR 58),
// such as the Linux console and GNU screen: there the white underline of the
// active row would be in the text's color instead, so a shaded row is the
// better default (see ROW_MARK_* in editor/settings.hpp).
bool terminal_shows_underline_color();
