#pragma once

#include <filesystem>

// the whole editor on screen: the top bar, the explorer, the main view (tabs,
// the editor, the status bar, the command line) and the menu tabs, put
// together and run until the user quits.

struct AppOptions {
    std::filesystem::path folder;       // the folder shown in the explorer
    bool single_file = false;           // a file was given: no explorer
    std::filesystem::path config_path;  // where changed settings are saved
};

// runs the editor; returns the program's exit code.
int run_app(const AppOptions& options);
