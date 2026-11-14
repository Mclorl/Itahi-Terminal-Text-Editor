#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "core/file_io.hpp"
#include "editor/config.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "platform/terminal.hpp"
#include "syntax/theme.hpp"
#include "ui/app_view.hpp"

namespace fs = std::filesystem;

// itahi [file or folder]: reads the arguments and the settings, opens the
// file or folder, and runs the editor (ui/app_view.cpp).
int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "\nHow to use: itahi [file or folder]\n" << std::endl;
        return 1;
    }

    // one status check, without throwing. A path that doesn't exist yet is a
    // new file, created by the first ">w".
    std::error_code ec;
    const fs::file_status arg_status = argc > 1 ? fs::status(argv[1], ec) : fs::file_status{}; // check the first argument if it is a file or a directory.
    const bool arg_is_new_file = argc > 1 && arg_status.type() == fs::file_type::not_found; // returns true whether the argument does not exist using arg_status.
    const bool arg_is_file = argc > 1 && (fs::is_regular_file(arg_status) || arg_is_new_file); // returns true whether the argument is a file using arg_status and arg_is_new_file if true.
    const bool arg_is_dir = argc > 1 && fs::is_directory(arg_status); // returns true whether the argument is a directory.

    if (argc > 1 && !arg_is_file && !arg_is_dir) {
        // if both are false then don't open.
        std::cerr << "Cannot open " << argv[1] << (ec ? ": " + ec.message() : std::string()) << std::endl;
        return 1;
    }

    // the settings: the terminal decides how the active row is marked by
    // default, the config file has the rest (and can change that too).
    settings.active_row_mark = terminal_shows_underline_color() ? ROW_MARK_WHITE_LINE : ROW_MARK_SHADED;
    add_syntax_config_entries();
    const fs::path config_path = default_config_path(); // sets the default config path based on the system.
    std::string config_error;
    if (!load_config(config_path, config_error)) {
        command_error = config_error;  // shown on the first frame
    }

    // current directory then open the file.
    const fs::path folder = arg_is_dir ? fs::path(argv[1]) : fs::path("."); 
    workspace.root = arg_is_file ? fs::path(argv[1]).parent_path().string() : folder.string();
    if (workspace.root.empty()) {
        workspace.root = ".";
    }

    if (arg_is_file) {
        // single file mode: open it now (a missing file becomes a new buffer).
        std::string error;
        if (!workspace.open(argv[1], error)) {
            std::cerr << "Cannot open " << error << std::endl;
            return 1;
        }
    } else {
        std::string error;
        list_directory(folder, error);
        if (!error.empty()) {
            std::cerr << "Cannot open " << error << std::endl;
            return 1;
        }
    }

    return run_app({folder, arg_is_file, config_path});
}
