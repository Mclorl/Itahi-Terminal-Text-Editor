#include "editor/motion.hpp"

#include <algorithm>
#include <cctype>

namespace {

    enum class Kind { Space, Word, Punctuation };

    Kind kind_of(unsigned char c) {
        if (c >= 0x80 || std::isalnum(c) || c == '_') {
            return Kind::Word;
        }
        if (std::isspace(c)) {
            return Kind::Space;
        }
        return Kind::Punctuation;
    }

}  // namespace

std::size_t word_right(std::string_view text, std::size_t from) {
    std::size_t at = std::min(from, text.size());
    while (at < text.size() && kind_of(static_cast<unsigned char>(text[at])) == Kind::Space) {
        ++at;
    }
    if (at == text.size()) {
        return at;
    }
    const Kind kind = kind_of(static_cast<unsigned char>(text[at]));
    while (at < text.size() && kind_of(static_cast<unsigned char>(text[at])) == kind) {
        ++at;
    }
    return at;
}

std::size_t word_left(std::string_view text, std::size_t from) {
    std::size_t at = std::min(from, text.size());
    while (at > 0 && kind_of(static_cast<unsigned char>(text[at - 1])) == Kind::Space) {
        --at;
    }
    if (at == 0) {
        return 0;
    }
    const Kind kind = kind_of(static_cast<unsigned char>(text[at - 1]));
    while (at > 0 && kind_of(static_cast<unsigned char>(text[at - 1])) == kind) {
        --at;
    }
    return at;
}

std::size_t next_char(std::string_view text, std::size_t from) {
    if (from >= text.size()) {
        return text.size();
    }
    std::size_t at = from + 1;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) {
        ++at;  // continuation byte
    }
    return at;
}

std::size_t previous_char(std::string_view text, std::size_t from) {
    std::size_t at = std::min(from, text.size());
    if (at == 0) {
        return 0;
    }
    --at;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) {
        --at;
    }
    return at;
}
