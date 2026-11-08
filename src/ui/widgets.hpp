#pragma once

// small reusable pieces of the UI: buttons, checkboxes, the numeric input,
// scroll frame and scroll bar, box capture.

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <string>

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

// underlines the whole row `row` of the element (counted from its top). The
// underline is always white, whatever the color of the text on that row.
ftxui::Decorator underline_row(int row);

// underlines every cell of the element, in white like underline_row().
ftxui::Element underlined_white(ftxui::Element child);


// width and digits of the numeric settings fields.
inline constexpr int MAX_WIDTH_INPUT_INT_DEFAULT = 4;
inline constexpr int MAX_DIGITS_LENGTH_DEFAULT = 2;

ftxui::Component make_numeric_input_row(std::string& target_var, const std::string& label_text, int max_digits_taken, int max_width);

ftxui::ButtonOption create_plain_button_option_symbol();

ftxui::ButtonOption create_plain_button_option();

ftxui::CheckboxOption create_plain_checkbox_option();

ftxui::Component create_text_button(const std::string& label, int tab_index, int &selected_tab, int &view_status);