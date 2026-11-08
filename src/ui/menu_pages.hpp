#pragma once

#include <ftxui/component/component.hpp>

class EditorView;

// the rows of the menu tabs (File, Edit, View, Settings, Help).

// --- File Back Option ---
ftxui::Component file_back_button_func();

// --- File Menu Choices ---
ftxui::Component file_btn_new_file_func();
ftxui::Component file_btn_save_file_func();
ftxui::Component file_btn_close_file_func();

// --- Edit Menu Choices ---
ftxui::Component edit_btn_undo_func();
ftxui::Component edit_btn_redo_func();
// the clipboard and the selection of the editor `view`.
ftxui::Component edit_btn_cut_func(EditorView& view);
ftxui::Component edit_btn_copy_func(EditorView& view);
ftxui::Component edit_btn_paste_func(EditorView& view);
ftxui::Component edit_btn_select_all_func(EditorView& view);

// --- View Menu Choices ---
ftxui::Component toggle_linenums_func();
ftxui::Component toggle_underline_active_row_on_numbers_func();
ftxui::Component toggle_underline_active_row_func();
// "Active Row Style:" dropdown: White underline / Shaded background / Underline in text color.
ftxui::Component active_row_style_row_func();
ftxui::Component toggle_syntax_highlighting_func();
ftxui::Component toggle_word_wrap_func();
ftxui::Component toggle_status_bar_func();
ftxui::Component toggle_tabs_func();
// "Theme:" dropdown: sets every syntax color (and the editor's accents) at once.
ftxui::Component theme_row_func();
// one color picker per syntax group.
ftxui::Component syntax_color_settings_func();

// --- Settings Menu Choices ---
ftxui::Component mouse_sens_row_func();
ftxui::Component tab_size_row_func();
ftxui::Component toggle_tab_inserts_spaces_func();
// "Scroll style:" dropdown: Keep cursor centered / Scroll freely.
ftxui::Component scroll_style_row_func();

// --- Help Menu Choices ---
ftxui::Component btn_about_func();
ftxui::Component btn_ftxui_about_func();
ftxui::Component warning_text_link_direct_func();
