#include <doctest/doctest.h>

#include "core/encoding.hpp"

TEST_CASE("text is not flagged as binary") {
    CHECK_FALSE(is_binary_or_unsupported_text("hello\n"));
    CHECK_FALSE(is_binary_or_unsupported_text("\tindented\r\n"));
    CHECK_FALSE(is_binary_or_unsupported_text("\xE6\x97\xA5\xE6\x9C\xAC"));  // Japanese
    CHECK_FALSE(is_binary_or_unsupported_text("\xF4\x8F\xBF\xBF"));          // U+10FFFF
}

TEST_CASE("binary and broken UTF-8 are flagged") {
    CHECK(is_binary_or_unsupported_text(std::string("a\0b", 3)));  // NUL byte
    CHECK(is_binary_or_unsupported_text("\x1B[2J"));                // terminal escape
    CHECK(is_binary_or_unsupported_text("\xE6\x97"));               // cut off
    CHECK(is_binary_or_unsupported_text("\xED\xA0\x80"));           // surrogate
    CHECK(is_binary_or_unsupported_text("\xE0\x80\x80"));           // overlong
    CHECK(is_binary_or_unsupported_text("\xF4\x90\x80\x80"));       // above U+10FFFF
    CHECK(is_binary_or_unsupported_text("caf\xE9"));                // Latin-1
}
