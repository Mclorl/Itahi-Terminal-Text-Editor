#include <filesystem>
#include <system_error>
#include <vector>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <sstream>

#include "../library/file_entry.hpp"

#include "../library/file_system_left_panel.hpp"

namespace fs = std::filesystem;




void opened_directory_file_entry(std::vector<ftxui::Component> &array, std::function<ftxui::ButtonOption(void)> styling, const std::vector<std::string>& file_paths, std::string &content, std::string &active_file) {
    // reference: left_panel_button_file_arr.push_back(ftxui::Button ("Hello world!", []{}, left_panel_file_styling()));
    for (const std::string path : file_paths) {
        // extract just the file name for clean display
        fs::path p(path);
        std::string file_name = p.filename().string();
        
        if (fs::is_regular_file(fs::status(p))) {
            array.push_back(ftxui::Button(file_name, [p, file_name, &content, &active_file]() {
                // callback when a file entry button is clicked
                content = readFile(p);
                active_file = file_name;
            }, styling()));
        }
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

void opened_directory_entry(std::vector<ftxui::Component> &array, std::function<ftxui::ButtonOption(void)> styling, const std::vector<std::string>& file_paths) {
    for (const std::string path : file_paths) {
        // extract just the directory name for clean display
        fs::path p(path);
        std::string file_name = p.filename().string();
        
        if (fs::is_directory(fs::status(p))) {
            array.push_back(ftxui::Button(file_name, []() {
                // callback when a file entry button is clicked

            }, styling()));
        }
    }
}