#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <unordered_set>

// custom input styling variable.
extern int cursor_index;

// custom unput styling functions.

ftxui::InputOption style_file_content_input_option(int &cursor_index);

// line increment in every line.
ftxui::Component file_content_count_component(std::string &content,
    bool &show_line_numbers,
    int &cursor_index,
    bool &underline_active_row_on_numbers
);

ftxui::ButtonOption left_panel_file_styling();

ftxui::ButtonOption left_panel_directory_styling();

ftxui::Component make_numeric_input_row(std::string& target_var, std::string label_text, int max_digits_taken, int max_width);

ftxui::ButtonOption create_plain_button_option_symbol();

ftxui::ButtonOption create_plain_button_option();

ftxui::CheckboxOption create_plain_checkbox_option();

ftxui::Component create_text_button(std::string label, int tab_index, int &tab_selected, int &status);