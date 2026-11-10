#pragma once

#include <string>

// every setting of the View and Settings tabs, in one place. Saved in the
// config file (see editor/config.hpp) whenever one changes.

// how the editor scrolls. Ints because the dropdowns that set them work on an
// index.
//   SCROLL_KEEP_CURSOR_CENTERED  the view follows the cursor and keeps it in
//                                the middle; the mouse wheel moves the cursor
//   SCROLL_FREELY                the wheel moves only the view, the cursor can
//                                go off screen; moving the cursor scrolls just
//                                enough to show it again
inline constexpr int SCROLL_KEEP_CURSOR_CENTERED = 0;
inline constexpr int SCROLL_FREELY = 1;

// how the cursor's row is marked when "Show Underline On Active Row" is on.
//   ROW_MARK_WHITE_LINE  a dim gray underline (the terminal's underline color,
//                        SGR 58; terminals without it underline in the text's color)
//   ROW_MARK_SHADED      a gray background instead: looks the same in every terminal
//   ROW_MARK_TEXT_COLOR  an underline in the color of the text above it
inline constexpr int ROW_MARK_WHITE_LINE = 0;
inline constexpr int ROW_MARK_SHADED = 1;
inline constexpr int ROW_MARK_TEXT_COLOR = 2;

struct Settings {
    // -- Settings tab --
    std::string mouse_sensitivity = "3";  // as typed: see get_mouse_sensitivity()
    std::string tab_size = "4";           // as typed: see get_tab_size()
    bool tab_inserts_spaces = true;       // Tab in Edit mode: spaces, or a tab character
    int scroll_style = SCROLL_KEEP_CURSOR_CENTERED;
    // unsaved changes are copied to the state folder every few seconds, and
    // offered back after a crash or a closed terminal (editor/recovery.hpp).
    bool keep_recovery_copies = true;
    // a file changed by another program is read again, unless it has unsaved
    // changes here.
    bool auto_reload = true;
    // search: upper and lower case alike (Alt+C), whole words only (Alt+W).
    bool search_ignore_case = false;
    bool search_whole_word = false;

    // -- View tab --
    bool show_line_numbers = true;
    bool underline_active_row_on_numbers = false;
    bool underline_active_row = false;
    int active_row_mark = ROW_MARK_WHITE_LINE;
    bool syntax_highlighting = true;
    bool word_wrap = false;
    bool show_status_bar = true;
    bool show_tabs = true;
    int theme = 0;  // index into themes() (syntax/theme.hpp)

    // -- kept between runs, not in a tab --
    int explorer_width = 25;  // columns, changed by dragging its separator
};

// the explorer can't be dragged narrower or wider than this.
inline constexpr int min_explorer_width = 25;
inline constexpr int max_explorer_width = 100;

extern Settings settings;

// the numbers typed in the Settings tab, with safe fallbacks.

// mouse wheel: rows per notch (1 - 99, default 1 when empty or invalid).
int get_mouse_sensitivity();

// columns per indent level and per tab character (1 - 16, default 4).
int get_tab_size();
