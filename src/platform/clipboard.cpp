#include "platform/clipboard.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

#include "platform/process.hpp"

namespace {

    std::string& last_copied() {
        static std::string text;
        return text;
    }

    bool has_env(const char* name) {
        const char* value = std::getenv(name);
        return value && *value;
    }

    // the programs that can write / read the system clipboard here, best first.
    std::vector<std::vector<std::string>> copy_programs() {
        std::vector<std::vector<std::string>> list;
        if (has_env("WAYLAND_DISPLAY")) {
            list.push_back({"wl-copy"});
        }
        if (has_env("DISPLAY")) {
            list.push_back({"xclip", "-selection", "clipboard"});
            list.push_back({"xsel", "--clipboard", "--input"});
        }
        return list;
    }

    std::vector<std::vector<std::string>> paste_programs() {
        std::vector<std::vector<std::string>> list;
        if (has_env("WAYLAND_DISPLAY")) {
            list.push_back({"wl-paste", "--no-newline"});
        }
        if (has_env("DISPLAY")) {
            list.push_back({"xclip", "-selection", "clipboard", "-o"});
            list.push_back({"xsel", "--clipboard", "--output"});
        }
        return list;
    }

}  // namespace

std::string base64_encode(std::string_view data) {
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const unsigned n = static_cast<unsigned char>(data[i]) << 16 | static_cast<unsigned char>(data[i + 1]) << 8 |
                           static_cast<unsigned char>(data[i + 2]);
        out += digits[n >> 18 & 63];
        out += digits[n >> 12 & 63];
        out += digits[n >> 6 & 63];
        out += digits[n & 63];
    }
    if (i + 1 == data.size()) {
        const unsigned n = static_cast<unsigned char>(data[i]) << 16;
        out += digits[n >> 18 & 63];
        out += digits[n >> 12 & 63];
        out += "==";
    } else if (i + 2 == data.size()) {
        const unsigned n = static_cast<unsigned char>(data[i]) << 16 | static_cast<unsigned char>(data[i + 1]) << 8;
        out += digits[n >> 18 & 63];
        out += digits[n >> 12 & 63];
        out += digits[n >> 6 & 63];
        out += '=';
    }
    return out;
}

std::string osc52_sequence(std::string_view text) {
    return "\x1B]52;c;" + base64_encode(text) + "\x07";
}

std::string clipboard_copy(const std::string& text) {
    last_copied() = text;

    // the terminal: written straight to it between two frames; terminals
    // that don't know the sequence ignore it.
    std::cout << osc52_sequence(text) << std::flush;

    for (const auto& program : copy_programs()) {
        if (!program_exists(program[0])) {
            continue;
        }
        const ProcessResult result = run_process(program, text, false, 1500);
        if (result.started && result.exit_code == 0) {
            return "the clipboard (" + program[0] + ")";
        }
    }
    return "the clipboard";
}

std::string clipboard_paste(bool& from_system) {
    for (const auto& program : paste_programs()) {
        if (!program_exists(program[0])) {
            continue;
        }
        const ProcessResult result = run_process(program, "", true, 1500);
        if (result.started && result.exit_code == 0) {
            from_system = true;
            return result.output;
        }
    }
    from_system = false;
    return last_copied();
}
