#pragma once

#include <string>

// opens `url` in the browser without blocking the editor; returns false when
// the opener could not be started.
bool open_url(const std::string& url);
