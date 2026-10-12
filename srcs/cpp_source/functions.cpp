#include <iostream>

#include "../library/functions.hpp"

#include "../library/custom_styling.hpp"
#include "../library/global_variables.hpp"

#include "../library/app_data.hpp"

#include "../library/file_entry.hpp"

// functions

// tab functions
// --- File Back Option ---
ftxui::Component file_back_button_func() {
    return ftxui::Button("<-", [] {
        current_events = "Action Executed: Back Button Pressed"; // change soon to go back to the text editor. Function.
        status = 1;
        tab_selected = 0;
    }, create_plain_button_option_symbol());
}
// --- File Menu Choices ---
ftxui::Component file_toggle_autosave_func() {
    return ftxui::Checkbox("Enable Auto-Save", &auto_save, create_plain_checkbox_option());
}

ftxui::Component file_btn_new_file_func() { 
    return ftxui::Button("New File", [] {
        current_events = "Action Executed: Created New File";
    }, create_plain_button_option());
}

ftxui::Component file_btn_save_file_func() { 
    return ftxui::Button("Save File", [] {
        current_events = write_file_entry(long_active_file_name, content);
    }, create_plain_button_option());
}
// --- Edit Menu Choices ---
// auto edit_toggle_word_wrap = ftxui::Checkbox("Enable Word Wrap", create_plain_checkbox_option());
ftxui::Component edit_btn_undo_func() { 
    return ftxui::Button("Undo", [] {
        current_events = "Action Executed: Undo";
    }, create_plain_button_option());
}
ftxui::Component edit_btn_redo_func() {
    return ftxui::Button("Redo", [] {
        current_events = "Action Executed: Redo";
    }, create_plain_button_option());
}
// --- View Menu Choices ---
ftxui::Component toggle_linenums_func() { 
    return ftxui::Checkbox("Show Line Numbers", &show_line_numbers, create_plain_checkbox_option());
}

ftxui::Component toggle_underline_active_row_on_numbers_func() {
    return ftxui::Checkbox("Show Underline On Active Row On Numbers", &underline_active_row_on_numbers, create_plain_checkbox_option());
}
// ftxui::Component toggle_statusbar = ftxui::Checkbox("Show Status Bar", &show_status_bar, create_plain_checkbox_option());

// --- Terminal Menu Choices ---
// ftxui::Component btn_clear_term() {
//    return ftxui::Button("Clear Output Console", [&] {
    //     current_events = "Action Executed: Terminal Cleared";
    // }, create_plain_button_option());
// }
// ftxui::Component btn_build_project = ftxui::Button("Run CMake Build", [&] {
//     current_events = "Action Executed: CMake Build Started...";
// }, create_plain_button_option());

// --- Settings Menu Choices ---
ftxui::Component mouse_sens_row_func() {
    return make_numeric_input_row(mouse_sensitivity, "Mouse Sensitivity: ", MAX_DIGITS_LENGTH_DEFAULT, MAX_WIDTH_INPUT_INT_DEFAULT);
}
// --- Help Menu Choices ---
ftxui::Component btn_about_func() {
    return ftxui::Button("About Itahi Editor", [] {
        current_events = "Directing To Itahi Editor Repository.";
        open_url("https://github.com/Mclorl/Itahi-Terminal-Text-Editor");
    }, create_plain_button_option());
}
ftxui::Component btn_ftxui_about_func() {
    return ftxui::Button("About FTXUI", [] {
        current_events = "Directing To FTXUI Repository.";
        open_url("https://github.com/ArthurSonzogni/FTXUI");
    }, create_plain_button_option());
}

ftxui::Component warning_text_link_direct_func() { 
    return ftxui::Button("Once clicked it will direct you to a website!", []{
        current_events = "Once you click one of the buttons below it will direct you to the github repository.";
    }, create_plain_button_option());
}

// mouse sensitivity input fallback function.
int get_mouse_sensitivity() {
    try {
        return mouse_sensitivity.empty() || mouse_sensitivity == ""  || mouse_sensitivity.size() == 0 ? 1 : std::stoi(mouse_sensitivity);
    } catch (const std::exception&) {
        return 1;
    }
}

namespace {

    // callback and the mode handler below.
    void refresh_unsaved_status(std::string &content) {
        if (content_read_first[long_active_file_name] != content) {
            content_user_updated[long_active_file_name] = content;
            active_file = active_file_not_saved;
            file_unsaved_status[long_active_file_name] = true;
        } else {
            active_file = active_file_saved;
            file_unsaved_status[long_active_file_name] = false;
        }
    }

    // central place to switch modes, so the three flags can never disagree.
    void set_normal_mode() { is_normal_mode = true;  is_edit_mode = false; is_command_mode = false; }
    void set_insert_mode() { is_normal_mode = false; is_edit_mode = true;  is_command_mode = false; }


}  // namespace

ftxui::Component file_content_input(std::string &content, int &cursor_index) {
    ftxui::InputOption custom_input_style_update = style_file_content_input_option(cursor_index);

    custom_input_style_update.on_change = [&content](void) { // no delay on updating the active_file
        refresh_unsaved_status(content);
    };

    ftxui::Component main_user_input = ftxui::Input(&content, "Type something...", custom_input_style_update);

    // raw pointer to the Input on purpose: capturing the shared_ptr inside its
    // own CatchEvent would create a reference cycle.
    ftxui::ComponentBase* input_ptr = main_user_input.get();

    // normal-mode key handling.
    return ftxui::CatchEvent(main_user_input, [&content, input_ptr](ftxui::Event event) {
        // never swallow mouse events: the Input needs them to take focus (and
        // to place the cursor). Swallowing them is why the cursor vanished.
        if (event.is_mouse()) {
            return false;
        }

        // --- Edit mode ---

        // Enter edit mode
        if (is_edit_mode) {
            // Insert mode: type normally. Escape goes back to normal mode.
            if (event == ftxui::Event::Escape) {
                set_normal_mode();
                return true;
            }
            return false;  // forwarded to the Input
        }

        // --- Normal mode ---
        refresh_unsaved_status(content);

        // h/j/k/l act as arrow keys.
        if (event == ftxui::Event::Character('h') && is_normal_mode) { input_ptr->OnEvent(ftxui::Event::ArrowLeft);  return true; }
        if (event == ftxui::Event::Character('j') && is_normal_mode) { input_ptr->OnEvent(ftxui::Event::ArrowDown);  return true; }
        if (event == ftxui::Event::Character('k') && is_normal_mode) { input_ptr->OnEvent(ftxui::Event::ArrowUp);    return true; }
        if (event == ftxui::Event::Character('l') && is_normal_mode) { input_ptr->OnEvent(ftxui::Event::ArrowRight); return true; }

        // Enter insert mode
        if (event == ftxui::Event::Character('e') && is_normal_mode) {
            set_insert_mode();
            input_ptr->TakeFocus();  // keeps the cursor visible in the editor
            return true;
        }

        // Real arrows / Home / End / Page keys keep working (forwarded).
        // Printable characters and editing keys are consumed so normal mode
        // can never modify the buffer.
        if (event.is_character() || event == ftxui::Event::Return || event == ftxui::Event::Backspace || event == ftxui::Event::Delete) {
            return true;
        }

        return false;
    });
}

bool check_content_directory_for_unsaved_file(std::string path) {
    std::string directory_path = iterate_current_path_shallow(path);

    std::stringstream s(directory_path);
    std::string line;

    while (std::getline(s, line)) {
        if (fs::is_directory(line)) {
            if (check_content_directory_for_unsaved_file(line)) {
                return true;
            }
        }

        if (file_unsaved_status[line] && fs::is_regular_file(line)) {
            return true;
        } 
    }

    return false;
}

// note: not final, it is a bit messy to look at. Handle the terminal_handling and this soon.