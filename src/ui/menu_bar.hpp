#pragma once

#include <string>

#include <ftxui/component/component.hpp>

// the menu bar (File, Edit, View, Settings, Help), the open file's name and
// the current mode.
ftxui::Component make_top_bar(const std::string &active_file_name);
