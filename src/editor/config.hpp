#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// the config file: the settings of the View and Settings tabs, kept between
// runs. One "name = value" per line, '#' starts a comment:
//
//     tab_size = 4
//     word_wrap = true
//     theme = Ember
//
// Itahi writes it whenever a setting changes, so it never has to be edited
// by hand (but it can be).

// a saved setting: its name, and how to read and set its value as text.
// `set` returns false for a value it can't use (the setting stays as it was).
struct ConfigEntry {
    std::string name;
    std::function<std::string()> get;
    std::function<bool(const std::string&)> set;
};

// every saved setting: the editor's own (editor/settings.hpp), plus the ones
// other parts add with add_config_entry() (the syntax colors).
const std::vector<ConfigEntry>& config_entries();
// adds an entry, or replaces the one with the same name.
void add_config_entry(ConfigEntry entry);

// $XDG_CONFIG_HOME/itahi/config, or ~/.config/itahi/config.
std::filesystem::path default_config_path();

// the current settings, as the file's text.
std::string config_text();

// applies the settings in `text`. Returns what was wrong with it (unknown
// names, invalid values), one message per line; the rest still applies.
std::vector<std::string> apply_config_text(const std::string& text);

// reads `path` at start. A missing file is fine (the defaults stay). False,
// with `error` set, when the file has problems or can't be read.
bool load_config(const std::filesystem::path& path, std::string& error);

// writes `path` when the settings differ from what was last loaded or saved
// there. False, with `error` set, when it can't be written.
bool save_config_if_changed(const std::filesystem::path& path, std::string& error);
