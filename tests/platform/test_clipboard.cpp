#include <doctest/doctest.h>

#include "platform/clipboard.hpp"

TEST_CASE("text for the terminal's clipboard (OSC 52) is base64") {
    CHECK(base64_encode("") == "");
    CHECK(base64_encode("f") == "Zg==");
    CHECK(base64_encode("fo") == "Zm8=");
    CHECK(base64_encode("foo") == "Zm9v");
    CHECK(base64_encode("foobar") == "Zm9vYmFy");
    CHECK(base64_encode("\xE6\x97\xA5") == "5pel");   // UTF-8 bytes as they are
    CHECK(osc52_sequence("hi") == "\x1B]52;c;aGk=\x07");
}
