#include "core/encoding.hpp"

bool is_binary_or_unsupported_text(const std::string& data) {
    const std::size_t size = data.size();
    std::size_t i = 0;

    while (i < size) {
        const unsigned char byte = static_cast<unsigned char>(data[i]);

        if (byte < 0x80) {
            // control codes (NUL, ESC, ...) would be sent to the terminal as
            // they are and break the display. Tab, newlines and form feed are
            // normal in text files.
            if (byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\r' && byte != '\f') {
                return true;
            }
            i++;
            continue;
        }

        // UTF-8 multi-byte sequence: the lead byte gives its length, and
        // limits the second byte (no overlong forms, surrogates or values
        // above U+10FFFF).
        std::size_t length = 0;
        unsigned char low = 0x80;
        unsigned char high = 0xBF;
        if (byte >= 0xC2 && byte <= 0xDF) {
            length = 2;
        } else if (byte >= 0xE0 && byte <= 0xEF) {
            length = 3;
            if (byte == 0xE0) low = 0xA0;   // overlong
            if (byte == 0xED) high = 0x9F;  // surrogates
        } else if (byte >= 0xF0 && byte <= 0xF4) {
            length = 4;
            if (byte == 0xF0) low = 0x90;   // overlong
            if (byte == 0xF4) high = 0x8F;  // above U+10FFFF
        } else {
            return true; // not a valid lead byte
        }

        if (i + length > size) {
            return true; // cut off at the end of the file
        }
        const unsigned char second = static_cast<unsigned char>(data[i + 1]);
        if (second < low || second > high) {
            return true;
        }
        for (std::size_t k = 2; k < length; k++) {
            if ((static_cast<unsigned char>(data[i + k]) & 0xC0) != 0x80) {
                return true; // not a continuation byte
            }
        }
        i += length;
    }

    return false;
}
