#include <filesystem>
#include <system_error>
#include <vector>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <sstream>
#include <algorithm>
#include <ftxui/screen/string.hpp>

#include <unordered_set>

#include "../library/file_entry.hpp"

#include "../library/file_system_left_panel.hpp"

#include "../library/global_variables.hpp"

#include "../library/functions.hpp"

namespace fs = std::filesystem;

std::string truncate_name(const std::string& name, int available_width, std::string path) {
    if (available_width <= 0) {
        return {};
    }

    if (static_cast<int>(name.size()) <= available_width) {
        file_truncate_name_bool_status[path] = false;
        return name;
    }

    std::string suffix = "...";

    if (available_width <= static_cast<int>(suffix.size())) {
        file_truncate_name_bool_status[path] = true;
        return suffix.substr(0, static_cast<std::size_t>(available_width));
    }

    file_truncate_name_bool_status[path] = true;
    const std::size_t name_width = static_cast<std::size_t>(available_width - suffix.size());

    return name.substr(0, name_width) + suffix;
}


int available_name_width(int panel_width, int indentation_level) {
    constexpr int border_width = 4;
    constexpr int indentation_width = 1;
    constexpr int scrollbar_width = 1;

    int available_width = panel_width - border_width - scrollbar_width - indentation_level * indentation_width;

    available_width = std::max(1, available_width);

    return std::max(1, available_width);
}

void opened_directory_file_entry(ftxui::Component container, std::vector<ftxui::Component> &array, std::function<ftxui::ButtonOption(void)> styling, const std::vector<std::string>& file_paths, std::string &content_main_user_updated, std::string &active_file, std::string &long_active_file_name_local, int &left_panel_width, int total_width_size, std::unordered_map<std::string, bool> &content_already_read_local, std::string file_folder_path_key_access) {
    // reference: left_panel_button_file_arr.push_back(ftxui::Button ("Hello world!", []{}, left_panel_file_styling()));
    
    for (const std::string path : file_paths) {
        // extract just the file name for clean display
        fs::path p(path);

        std::error_code ec;
        fs::file_status status = fs::status(p, ec);

        if (ec || !fs::is_regular_file(status)) {
            continue;
        }

        std::string file_name = p.filename().string();
        
        // count the indentation.
        int total_name_width = available_name_width(left_panel_width, total_width_size);

        // check if the name is longer, if it is then just add "..." at the end of the name.
        std::string display_name = truncate_name(file_name, total_name_width, p.string());

        // initialize the file status. One time.
        if (content_already_read_local.find(p.string()) == content_already_read_local.end()) {
            content_already_read_local[p.string()] = false;
            content_read_first[p.string()] = readFile(p.string());
            content_user_updated[p.string()] = "";
        }
        
        ftxui::ButtonOption left_panel_file_styling_local = styling();

        left_panel_file_styling_local.transform = [p](const ftxui::EntryState& current_state) {
            ftxui::Element e; // the spaces here is just temporary.
            // soon add a function if the code has error then turn this to red.
            std::string file_display_name = current_state.label;
            file_display_name.erase(file_unsaved_status[p.string()] && file_truncate_name_bool_status[p.string()] ? file_display_name.length() - 1 : file_display_name.length());
            e = file_unsaved_status[p.string()] ? ftxui::color(ftxui::Color::Yellow1, ftxui::text("  " + file_display_name + "*")) : ftxui::text("  " + current_state.label + "");
            screen->PostEvent(ftxui::Event::Custom);
            
            if (current_state.focused) {
                return ftxui::bold(e);
            }
            return e;
        };

        auto button = ftxui::Button(display_name, [p, file_name, &content_already_read_local, &content_main_user_updated , &long_active_file_name_local]() {
            // callback when a file entry button is clicked
            
            // change status to already read file.
            // make the content_user_updated to update the content once the user type or updated the content_main_user_updated.
            if (!content_already_read_local[p.string()]) {
                content_already_read_local[p.string()] = true;
                content_main_user_updated = content_read_first[p.string()];
            } else if (!content_user_updated[p.string()].empty()) {
                content_main_user_updated = content_user_updated[p.string()];
            } else {
                content_main_user_updated = content_read_first[p.string()];
            }

            active_file_not_saved = file_name + "*";
            active_file_saved = file_name;
            long_active_file_name_local = p.string();
        }, left_panel_file_styling_local);

        array.push_back(button);
        container->Add(button);
        
    }
} // issue with this approach is that the program has no control over what is in the content because it is in the function. Fix soon.

// NOT USING ANYMORE.
// int parse_return_total_int(std::string content) {
//     std::stringstream s(content);
//     std::string temp_string;
//     int temp_val = 0;

//     while (s >> temp_string) {
//         temp_val++;
//     }

//     return temp_val;
// }


// iterate_current_path(get_current_path(ec).string());

void opened_directory_entry(ftxui::ScreenInteractive *screen, ftxui::Component &container, std::vector<ftxui::Component> &array, std::function<ftxui::ButtonOption()> styling, std::function<ftxui::ButtonOption(void)> file_styling, const std::vector<std::string>& file_paths, std::unordered_set<std::string>& opened_folders, std::string &content, std::string &active_file, std::string &long_active_file_name_local, ftxui::Component parent_container, int &left_panel_width, int total_width_size) {

    for (const std::string path : file_paths) {
        // extract just the directory name for clean display
        fs::path p(path);
        std::string file_name = p.filename().string();

        std::error_code ec;
        fs::file_status status = fs::status(p, ec);

        if (ec || !fs::is_directory(status)) {
            continue;
        }

        // count the indentation.
        int total_name_width = available_name_width(left_panel_width, total_width_size);

        // check if the name is longer, if it is then just add "..." at the end of the name.
        std::string display_name = truncate_name(file_name, total_name_width, p.string());

        // DEBUG CODE FOR FOLDER OPENED STATUS IN THE ENTIRE RUNTIME.
        // if (folder_opened_status[p.string()]) {
        //     display_name = truncate_name(file_name, total_name_width) + " t";
        // } else {
        //     display_name = truncate_name(file_name, total_name_width) + " f";
        // }

        // seperate container for each folder
        auto children_container = ftxui::Container::Vertical({});

        auto visible_children = ftxui::Maybe(
            children_container,
            [&opened_folders, path] {
                return opened_folders.count(path) != 0;
            }
        );

        auto folder_component = ftxui::Container::Vertical({});

        ftxui::ButtonOption left_panel_directory_styling_local = styling();

        left_panel_directory_styling_local.transform = [p, path, &opened_folders, &status, screen, &active_file](const ftxui::EntryState& current_state) {
            bool is_open = opened_folders.find(path) != opened_folders.end();

            std::string prefix_folder_status = is_open ? "v " : "> ";

            // const std::string current_directory_content_styling = iterate_current_path_shallow(path);

            // std::stringstream s_2_style(current_directory_content_styling);
            // std::string line_style;

            // while(std::getline(s_2_style, line_style)) {
            //     if (file_unsaved_status[line_style] && fs::is_regular_file(line_style)) {
            //         has_unsaved_file_inside_directory[p.string()] = true;
            //         screen->PostEvent(ftxui::Event::Custom);
            //         break;
            //     } else {
            //         has_unsaved_file_inside_directory[p.string()] = false;
            //     }
                
            //     if (fs::is_directory(line_style)) {
                    
            //         screen->PostEvent(ftxui::Event::Custom);
            //         break;
            //     } else {
            //         has_unsaved_file_inside_directory[p.string()] = false;
            //     }
            // }

            has_unsaved_file_inside_directory[p.string()] = check_content_directory_for_unsaved_file(p.string());

            ftxui::Element e = has_unsaved_file_inside_directory[p.string()] ? ftxui::color(ftxui::Color::Yellow1, ftxui::text(prefix_folder_status + current_state.label)) : ftxui::text(prefix_folder_status + current_state.label);

            if (current_state.focused) {
                return ftxui::bold(e);
            }
            return e;
        };
        
        auto button = ftxui::Button(display_name, [p, screen, &container, &array, styling, file_styling, path, &opened_folders, &content, &active_file, children_container, &left_panel_width, total_width_size, &long_active_file_name_local]() {
            // callback when a file entry button is clicked

            // close folder and its state.
            if (opened_folders.count(path)) {
                opened_folders.erase(path);

                screen->PostEvent(ftxui::Event::Custom);
                return;
            }

            // open folder and its state.
            opened_folders.insert(path);

            if (!folder_opened_status[p.string()]) {
                folder_opened_status[p.string()] = true;
            }

            children_container->DetachAllChildren();

            std::vector<std::string> folder_content;
            
            const std::string current_directory_content = iterate_current_path_shallow(path);

            std::stringstream s_2(current_directory_content);
            std::string line;

            while (std::getline(s_2, line)) {
                if (!line.empty()) {
                    folder_content.push_back(line);
                }
            }

            opened_directory_entry(screen, container, array, styling, file_styling, folder_content, opened_folders, content, active_file, long_active_file_name_local, children_container, left_panel_width, total_width_size + 1);
            opened_directory_file_entry(children_container, array, file_styling, folder_content, content, active_file, long_active_file_name_local, left_panel_width, total_width_size + 1, content_already_read, p.string());

            screen->PostEvent(ftxui::Event::Custom);

        }, left_panel_directory_styling_local);

        // after resizing—reconstruct its children immediately.
        if (opened_folders.count(path) != 0) {
            std::vector<std::string> folder_content;

            const std::string current_directory_content = iterate_current_path_shallow(path);

            std::stringstream stream(current_directory_content);
            std::string line;

            while (std::getline(stream, line)) {
                if (!line.empty()) {
                    folder_content.push_back(line);
                }
            }

            opened_directory_entry(screen, container, array, styling, file_styling, folder_content, opened_folders, content, active_file, long_active_file_name_local, children_container, left_panel_width, total_width_size + 1);
            opened_directory_file_entry(children_container, array, file_styling, folder_content, content, active_file, long_active_file_name_local, left_panel_width, total_width_size + 1, content_already_read, p.string());

        }

        // render the subfolder with indentation.
        auto indented_children = ftxui::Renderer(visible_children, [visible_children, &opened_folders, path, button] {
            if (opened_folders.find(path) == opened_folders.end()) {
                return ftxui::emptyElement();
            }

            auto separator = ftxui::color(ftxui::Color::RGB(150, 150, 150), ftxui::separator());

            if (!button->Focused()) {
                separator = ftxui::color(ftxui::Color::RGB(80, 80, 80), ftxui::separator());
            }

            return ftxui::hbox({separator, visible_children->Render()});
        });

        array.push_back(button);
        folder_component->Add(button);
        folder_component->Add(indented_children);

        parent_container->Add(folder_component);
    }
    
}