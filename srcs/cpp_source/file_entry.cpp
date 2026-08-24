#include <filesystem>
#include <system_error>
#include <string>
#include <fstream>

#include "../library/file_entry.hpp"

namespace fs = std::filesystem;

// if there is no argument passed when the program executes, then get the current directory and create a new file there if saved.
void new_entry() {

}


// if there is an argument and it is a file, then just open that file.
void write_file_entry(std::string file_name) {

}

std::string readFile(std::string argument_file) {
    std::ifstream readFile(argument_file);
    std::stringstream temp_content;
    if (readFile.is_open()) {
        
        temp_content << readFile.rdbuf();
    }

    return temp_content.str();
}

std::string iterate_current_path_shallow(std::string userPath) {
    std::string directory_content;
    for (auto const& dir_entry : fs::directory_iterator(userPath)) {
        directory_content += std::string(dir_entry.path()) + '\n';
    }

    return directory_content;
}

fs::path get_current_path(std::error_code& ec) {
    return fs::current_path(ec);
}