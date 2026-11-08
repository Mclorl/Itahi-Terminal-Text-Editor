#pragma once

#include <string>

#include <ftxui/component/component.hpp>

// the editor's text area: an ftxui::Input with Itahi's keys (modes, vim
// motions, indentation, Page keys, undo) on top.
ftxui::Component file_content_input(std::string &text, int &cursor);

ftxui::InputOption style_file_content_input_option(int &cursor);

// line numbers on the left of the text, the cursor's line highlighted.
ftxui::Component file_content_count_component(std::string &text,
    bool &numbers_on,
    int &cursor,
    bool &underline_active
);
