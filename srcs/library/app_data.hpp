#pragma once

#include <string>

#define ITAHI_VERSION (std::string)" v0.0.1"

// returns false when the browser could not be started.
bool open_url(const std::string& url);