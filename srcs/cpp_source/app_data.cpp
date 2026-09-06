#include "../library/app_data.hpp"

#include <string>

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