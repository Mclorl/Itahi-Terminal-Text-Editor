#include <filesystem>
#include <system_error>
#include <string>
#include <fstream>
#include <unordered_map>

#include "../library/file_entry.hpp"

namespace fs = std::filesystem;

// if there is no argument passed when the program executes, then get the current directory and create a new file there if saved.
void new_entry() {

}


// if there is an argument and it is a file, then just open that file.
std::string write_file_entry(std::string file_name, std::string &updated_content) {
    std::ofstream write_file(file_name);

    if (!write_file.is_open()) {
        return "No Selected File.";
    }

    write_file << updated_content;
    content_user_updated[long_active_file_name].clear(); // It will be better if the array is deleted instead of setting the string or value to empty. Change soon.
    content_read_first[long_active_file_name] = updated_content;
    
    // active_file update
    active_file = active_file_saved;
    file_unsaved_status[long_active_file_name] = false;
    write_file.close();
    screen->PostEvent(ftxui::Event::Custom);
    return "Saved Current File in " + file_name;
}

std::string readFile(std::string argument_file) {
    std::ifstream read_file(argument_file);
    std::stringstream temp_content;
    if (read_file.is_open()) {
        temp_content << read_file.rdbuf();
    }

    read_file.close();
    return temp_content.str();
}

std::string iterate_current_path_shallow(std::string userPath) {
    std::string directory_content;
    for (auto const& dir_entry : fs::directory_iterator(userPath)) {
        directory_content += std::string(dir_entry.path()) + '\n';
    }

    return directory_content;
}

bool is_binary_or_unsupported_text(const std::string& data) {
    const std::size_t size = data.size();
    std::size_t i = 0;

    while (i < size) {
        const unsigned char byte = static_cast<unsigned char>(data[i]);

        if (byte < 0x80) {
            // control codes (NUL, ESC, ...) would be sent to the terminal as
            // they are and break the display. Tab, newlines and form feed are
            // normal in text files.
            if (byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\r' && byte != '\f') {
                return true;
            }
            i++;
            continue;
        }

        // UTF-8 multi-byte sequence: the lead byte gives its length.
        std::size_t length = 0;
        if (byte >= 0xC2 && byte <= 0xDF) {
            length = 2;
        } else if (byte >= 0xE0 && byte <= 0xEF) {
            length = 3;
        } else if (byte >= 0xF0 && byte <= 0xF4) {
            length = 4;
        } else {
            return true; // not a valid lead byte
        }

        if (i + length > size) {
            return true; // cut off at the end of the file
        }
        for (std::size_t k = 1; k < length; k++) {
            if ((static_cast<unsigned char>(data[i + k]) & 0xC0) != 0x80) {
                return true; // not a continuation byte
            }
        }
        i += length;
    }

    return false;
}

bool is_file_hidden_from_editor(const std::string& path) {
    if (path.empty() || files_opened_anyway.count(path) != 0) {
        return false;
    }

    // checked once per file: this runs on every render.
    static std::unordered_map<std::string, bool> checked;
    const auto found = checked.find(path);
    if (found != checked.end()) {
        return found->second;
    }
    return checked[path] = is_binary_or_unsupported_text(content_read_first[path]);
}

fs::path get_current_path(std::error_code& ec) {
    return fs::current_path(ec);
}