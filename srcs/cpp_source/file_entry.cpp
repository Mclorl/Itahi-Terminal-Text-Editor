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
void write_file_entry(std::string file_name, std::string &updated_content) {
    std::ofstream write_file(file_name);

    if (write_file.is_open()) {
        write_file << updated_content;
    }

    write_file.close();
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

fs::path get_current_path(std::error_code& ec) {
    return fs::current_path(ec);
}