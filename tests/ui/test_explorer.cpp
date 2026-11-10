#include <doctest/doctest.h>

#include "ui/explorer.hpp"

TEST_CASE("names are cut by cell width, never inside a character") {
    CHECK(truncate_name("short.txt", 20) == "short.txt");
    CHECK(truncate_name("a_very_long_file_name.txt", 10) == "a_very_...");
    // each of these characters is 3 bytes and 2 cells wide.
    CHECK(truncate_name("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt", 6) == "\xE6\x97\xA5...");
}
