#include "editor/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

#include "core/file_io.hpp"
#include "editor/settings.hpp"

namespace fs = std::filesystem;

namespace {

    std::string trim(const std::string& s) {
        const std::size_t first = s.find_first_not_of(" \t\r");
        if (first == std::string::npos) {
            return "";
        }
        return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
    }

    ConfigEntry flag(const std::string& name, bool& value) {
        return {name,
                [&value] { return std::string(value ? "true" : "false"); },
                [&value](const std::string& text) {
                    if (text == "true" || text == "yes" || text == "on" || text == "1") {
                        value = true;
                        return true;
                    }
                    if (text == "false" || text == "no" || text == "off" || text == "0") {
                        value = false;
                        return true;
                    }
                    return false;
                }};
    }

    // a number kept as typed in the Settings tab (a string), from `low` to `high`.
    ConfigEntry number(const std::string& name, std::string& value, int low, int high) {
        return {name,
                [&value] { return value; },
                [&value, low, high](const std::string& text) {
                    if (text.empty() || text.size() > 4 ||
                        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); })) {
                        return false;
                    }
                    const int n = std::stoi(text);
                    if (n < low || n > high) {
                        return false;
                    }
                    value = text;
                    return true;
                }};
    }

    // a whole number from `low` to `high`.
    ConfigEntry integer(const std::string& name, int& value, int low, int high) {
        return {name,
                [&value] { return std::to_string(value); },
                [&value, low, high](const std::string& text) {
                    if (text.empty() || text.size() > 6 ||
                        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); })) {
                        return false;
                    }
                    const int n = std::stoi(text);
                    if (n < low || n > high) {
                        return false;
                    }
                    value = n;
                    return true;
                }};
    }

    // one of a few words, stored as its index.
    ConfigEntry choice(const std::string& name, int& value, std::vector<std::string> words) {
        return {name,
                [&value, words] {
                    return value >= 0 && value < static_cast<int>(words.size()) ? words[static_cast<std::size_t>(value)] : words[0];
                },
                [&value, words](const std::string& text) {
                    const auto found = std::find(words.begin(), words.end(), text);
                    if (found == words.end()) {
                        return false;
                    }
                    value = static_cast<int>(found - words.begin());
                    return true;
                }};
    }

    std::vector<ConfigEntry>& entries() {
        static std::vector<ConfigEntry> list = {
            // Settings tab
            number("mouse_sensitivity", settings.mouse_sensitivity, 1, 99),
            number("tab_size", settings.tab_size, 1, 16),
            flag("tab_inserts_spaces", settings.tab_inserts_spaces),
            choice("scroll_style", settings.scroll_style, {"centered", "free"}),
            flag("keep_recovery_copies", settings.keep_recovery_copies),
            flag("auto_reload", settings.auto_reload),
            flag("search_ignore_case", settings.search_ignore_case),
            flag("search_whole_word", settings.search_whole_word),
            // View tab
            flag("show_line_numbers", settings.show_line_numbers),
            flag("underline_active_row_on_numbers", settings.underline_active_row_on_numbers),
            flag("underline_active_row", settings.underline_active_row),
            choice("active_row_mark", settings.active_row_mark, {"white_line", "shaded", "text_color"}),
            flag("syntax_highlighting", settings.syntax_highlighting),
            flag("word_wrap", settings.word_wrap),
            flag("show_status_bar", settings.show_status_bar),
            flag("show_tabs", settings.show_tabs),
            // kept between runs
            integer("explorer_width", settings.explorer_width, min_explorer_width, max_explorer_width),
        };
        return list;
    }

    // what the file held after the last load or save, so an unchanged
    // setting doesn't rewrite it.
    std::optional<std::string>& last_written() {
        static std::optional<std::string> text;
        return text;
    }

}  // namespace

const std::vector<ConfigEntry>& config_entries() {
    return entries();
}

void add_config_entry(ConfigEntry entry) {
    std::vector<ConfigEntry>& list = entries();
    const auto same = std::find_if(list.begin(), list.end(), [&](const ConfigEntry& e) { return e.name == entry.name; });
    if (same != list.end()) {
        *same = std::move(entry);
    } else {
        list.push_back(std::move(entry));
    }
}

// sets the default config path.
fs::path default_config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME"); // this is for /home/user/.config
    if (xdg && *xdg) {
        return fs::path(xdg) / "itahi" / "config";
    }
    const char* home = std::getenv("HOME"); // this is for HOME + /.config
    return fs::path(home && *home ? home : ".") / ".config" / "itahi" / "config";
}

std::string config_text() {
    std::string text = "# Itahi settings, written by Itahi when one changes in the View or Settings tab.\n";
    for (const ConfigEntry& entry : entries()) {
        text += entry.name + " = " + entry.get() + "\n";
    }
    return text;
}

std::vector<std::string> apply_config_text(const std::string& text) {
    std::vector<std::string> problems;
    std::istringstream in(text);
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            problems.push_back("line " + std::to_string(number) + ": no '=' in \"" + line + "\"");
            continue;
        }
        const std::string name = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        const auto entry = std::find_if(entries().begin(), entries().end(), [&](const ConfigEntry& e) { return e.name == name; });
        if (entry == entries().end()) {
            problems.push_back("line " + std::to_string(number) + ": unknown setting " + name);
        } else if (!entry->set(value)) {
            problems.push_back("line " + std::to_string(number) + ": invalid value for " + name + ": " + value);
        }
    }
    return problems;
}

bool load_config(const fs::path& path, std::string& error) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        last_written().reset();
        return true;  // first run: the defaults
    }
    const std::optional<std::string> text = read_text_file(path, error);
    if (!text) {
        return false;
    }
    const std::vector<std::string> problems = apply_config_text(*text);
    // only a real change of a setting rewrites the file (and drops the lines
    // that had problems).
    last_written() = config_text();
    if (!problems.empty()) {
        error = path.string() + ": " + problems.front() + (problems.size() > 1 ? " (and " + std::to_string(problems.size() - 1) + " more)" : "");
        return false;
    }
    return true;
}

bool save_config_if_changed(const fs::path& path, std::string& error) {
    const std::string text = config_text();
    if (last_written() == text) {
        return true;
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Could not save the settings: " + path.parent_path().string() + ": " + ec.message();
        last_written() = text;  // don't retry on every key
        return false;
    }
    const IoResult result = save_atomic(path, text);
    last_written() = text;
    if (!result.ok) {
        error = "Could not save the settings: " + result.error;
        return false;
    }
    return true;
}
