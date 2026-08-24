#pragma once

#define ITAHI_VERSION (std::string)" v0.0.1"

#define SEPARATOR_TRANSPARENT ftxui::separatorEmpty()

#define MAX_WIDTH_INPUT_INT_DEFAULT 4

#define MAX_DIGITS_LENGTH_DEFAULT 2

auto create_plain_button_option = []() {};

void open_url(const std::string& url) {
#if defined(_WIN32)
  std::string command = "start " + url;
#elif defined(__APPLE__)
  std::string command = "open " + url;
#else
  std::string command = "xdg-open " + url;
#endif
  std::system(command.c_str());
}