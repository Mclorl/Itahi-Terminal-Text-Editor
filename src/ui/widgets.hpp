#pragma once

// small reusable pieces of the UI: buttons, checkboxes, the numeric input,
// scroll frame and scroll bar, box capture.

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <functional>
#include <string>
#include <vector>

// records the box assigned to an element during layout, without clipping it to
// the visible viewport (unlike `ftxui::reflect`). Used to keep the explorer's
// full content box / per-row boxes so the view can be scrolled independently of
// the selection.
ftxui::Decorator capture_box(ftxui::Box& box);

// vertical scroll bar on the right, always drawn white (unlike
// ftxui::vscroll_indicator, whose bar takes the color of the text under it).
ftxui::Element white_vscroll_indicator(ftxui::Element child);

// a vertical viewport that remembers its top row, unlike ftxui::frame (which
// re-centres on the focused element every frame). The wheel moves `top` by
// whole rows; keep_visible() scrolls only as much as needed.
struct ScrollState {
    int top = 0;
    int view_rows = 1;
    int content_rows = 1;
    void scroll_by(int rows);
    void keep_visible(int row);
};
ftxui::Element scroll_frame(ftxui::Element child, ScrollState& state);

// draws the white scroll bar in column `x` from row `y_min` to `y_max`, for a
// view showing rows [top, top + view rows) of `total_rows`. Every cell of the
// column is cleared first; no bar when everything fits.
void paint_scroll_bar(ftxui::Screen& screen, int x, int y_min, int y_max, int top, int total_rows);

// marks one cell of the cursor's row, the way `mark` (ROW_MARK_*, see
// editor/settings.hpp) says: a dim gray underline, a `shade` background, or an
// underline in the text's color. `x_min` is the first column of the row.
void mark_active_row_cell(ftxui::Screen& screen, int x, int y, int x_min, int mark, ftxui::Color shade);


// width and digits of the numeric settings fields.
inline constexpr int MAX_WIDTH_INPUT_INT_DEFAULT = 4;
inline constexpr int MAX_DIGITS_LENGTH_DEFAULT = 2;

ftxui::Component make_numeric_input_row(std::string& target_var, const std::string& label_text, int max_digits_taken, int max_width);

// --- the rows of the menu pages: every control has a margin around it ---

// a row of a menu page: `inner` with a margin on its left, where a bar shows
// while the row has the focus, and a dim `hint` (its shortcut) on its right.
ftxui::Component menu_row(ftxui::Component inner, std::string hint = "");
// a title above a group of rows, with an empty line above and below it.
ftxui::Component menu_title(std::string title);
// an empty line between rows.
ftxui::Component menu_gap();
// a page of rows; it scrolls when the rows don't fit.
ftxui::Component menu_page(ftxui::Components rows);
// the label in front of a control: the same width on every row.
ftxui::Element menu_label(const std::string& label);

ftxui::ButtonOption create_plain_button_option_symbol();

ftxui::ButtonOption create_plain_button_option();

ftxui::CheckboxOption create_plain_checkbox_option();

// a dropdown in Itahi's style instead of FTXUI's boxed one: closed it reads
// "< Choice v >" like the buttons; open, the choices are listed below it as
// "(*) Choice" / "( ) Other", like the checkboxes. Bold when focused.
// `on_change` runs when another choice is picked.
ftxui::Component create_plain_dropdown(const std::vector<std::string>* entries, int* selected,
                                       std::function<void()> on_change = {});

ftxui::Component create_text_button(const std::string& label, int tab_index, int &selected_tab, int &view_status);