#include "ui/glyphs.hpp"

#include <algorithm>
#include <string>

#include <ftxui/screen/string.hpp>

namespace {

    // the length of the UTF-8 sequence starting at `at`, or 0 when it isn't
    // a valid one. Sets `codepoint`.
    std::size_t utf8_sequence(std::string_view text, std::size_t at, std::size_t end, char32_t& codepoint) {
        const unsigned char lead = static_cast<unsigned char>(text[at]);
        std::size_t length = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            codepoint = lead & 0x1F;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            codepoint = lead & 0x0F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            codepoint = lead & 0x07;
        } else {
            return 0;
        }
        if (at + length > end) {
            return 0;
        }
        for (std::size_t i = 1; i < length; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[at + i]);
            if ((c & 0xC0) != 0x80) {
                return 0;
            }
            codepoint = (codepoint << 6) | (c & 0x3F);
        }
        // overlong forms, surrogates and values past U+10FFFF are invalid.
        if ((length == 3 && codepoint < 0x800) || (length == 4 && (codepoint < 0x10000 || codepoint > 0x10FFFF)) ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return 0;
        }
        return length;
    }

}  // namespace

void layout_line(std::string_view text, std::size_t begin, std::size_t end, int tab_size, std::vector<GlyphCell>& out) {
    out.clear();
    tab_size = std::max(tab_size, 1);
    end = std::min(end, text.size());
    int column = 0;
    std::size_t at = begin;
    while (at < end) {
        const unsigned char c = static_cast<unsigned char>(text[at]);
        if (c == '\t') {
            const int width = tab_size - column % tab_size;
            out.push_back({at, 1, column, width, false});
            column += width;
            ++at;
            continue;
        }
        if (c < 0x80) {
            // control characters (NUL, ESC, '\r', ...) are never drawn: sent
            // to the terminal they would be commands.
            const int width = c < 0x20 || c == 0x7F ? 0 : 1;
            out.push_back({at, 1, column, width, false});
            column += width;
            ++at;
            continue;
        }

        char32_t codepoint = 0;
        const std::size_t length = utf8_sequence(text, at, end, codepoint);
        if (length == 0) {
            out.push_back({at, 1, column, 1, true});
            column += 1;
            ++at;
            continue;
        }
        int width = 0;
        if (codepoint >= 0x80 && codepoint < 0xA0) {
            width = 0;  // C1 control characters
        } else {
            width = std::max(0, ftxui::string_width(std::string(text.substr(at, length))));
        }
        // a combining mark (no width of its own) belongs to the glyph before it.
        if (width == 0 && !(codepoint >= 0x80 && codepoint < 0xA0) && !out.empty() && out.back().width > 0 &&
            !out.back().invalid && text[out.back().byte] != '\t' && out.back().byte + out.back().size == at) {
            out.back().size += length;
        } else {
            out.push_back({at, length, column, width, false});
            column += width;
        }
        at += length;
    }
}

std::vector<std::size_t> wrap_rows(const std::vector<GlyphCell>& glyphs, std::string_view text, int width) {
    width = std::max(width, 1);
    std::vector<std::size_t> starts{0};
    int used = 0;
    std::size_t last_space = std::string::npos;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        if (used + glyphs[i].width > width && i > starts.back()) {
            const std::size_t start = last_space != std::string::npos && last_space + 1 > starts.back() ? last_space + 1 : i;
            starts.push_back(start);
            used = 0;
            for (std::size_t k = start; k < i; ++k) {
                used += glyphs[k].width;
            }
            last_space = std::string::npos;
        }
        used += glyphs[i].width;
        if (text[glyphs[i].byte] == ' ' || text[glyphs[i].byte] == '\t') {
            last_space = i;
        }
    }
    return starts;
}

int line_width(const std::vector<GlyphCell>& glyphs) {
    return glyphs.empty() ? 0 : glyphs.back().column + glyphs.back().width;
}
