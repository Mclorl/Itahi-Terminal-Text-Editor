#pragma once

#include <string>

// ITAHI_VERSION_STRING comes from project(... VERSION ...) in CMakeLists.txt.
#ifndef ITAHI_VERSION_STRING
#define ITAHI_VERSION_STRING "dev"
#endif

inline const std::string ITAHI_VERSION = std::string(" v") + ITAHI_VERSION_STRING;
