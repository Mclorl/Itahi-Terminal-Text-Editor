#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "editor/config.hpp"
#include "editor/settings.hpp"
#include "syntax/theme.hpp"
#include "test_helpers.hpp"

namespace fs = std::filesystem;
using test::TempDir;
using test::read_all;

TEST_CASE("settings are written as name = value and read back") {
    add_syntax_config_entries();
    const Settings saved = settings;

    settings.tab_size = "8";
    settings.word_wrap = true;
    settings.scroll_style = SCROLL_FREELY;
    const std::string text = config_text();
    CHECK(text.find("tab_size = 8\n") != std::string::npos);
    CHECK(text.find("word_wrap = true\n") != std::string::npos);
    CHECK(text.find("scroll_style = free\n") != std::string::npos);
    CHECK(text.find("theme = Itahi\n") != std::string::npos);
    CHECK(text.find("color.keyword = Yellow\n") != std::string::npos);

    settings = Settings{};
    CHECK(apply_config_text(text).empty());
    CHECK(settings.tab_size == "8");
    CHECK(settings.word_wrap);
    CHECK(settings.scroll_style == SCROLL_FREELY);

    // a problem is reported, the rest still applies.
    const auto problems = apply_config_text("# comment\ntab_size = 99\nnot_a_setting = 1\nword_wrap = false\nbroken line\n");
    CHECK(problems.size() == 3);
    CHECK(settings.tab_size == "8");          // 99 is out of range
    CHECK_FALSE(settings.word_wrap);
    settings = saved;
}

TEST_CASE("a theme sets every syntax color, a color line after it changes one") {
    add_syntax_config_entries();
    const Settings saved = settings;
    CHECK(apply_config_text("theme = Ember\ncolor.comment = White\n").empty());
    CHECK(settings.theme == 1);
    CHECK(group_settings()[static_cast<int>(SyntaxGroup::Keyword)].color_index == palette_index("Orange"));
    CHECK(group_settings()[static_cast<int>(SyntaxGroup::Comment)].color_index == palette_index("White"));
    CHECK_FALSE(apply_config_text("theme = Nope\n").empty());
    apply_theme(0);
    settings = saved;
}

TEST_CASE("the config file is written only when a setting changed") {
    TempDir dir;
    const fs::path path = dir.path / "itahi" / "config";
    std::string error;
    CHECK(load_config(path, error));          // no file yet: the defaults
    CHECK(save_config_if_changed(path, error));
    CHECK(fs::exists(path));                  // the first save writes it (and its folder)

    const auto written = fs::last_write_time(path);
    fs::remove(path);
    CHECK(save_config_if_changed(path, error));
    CHECK_FALSE(fs::exists(path));            // nothing changed: not written again
    (void)written;

    const std::string before = settings.mouse_sensitivity;
    settings.mouse_sensitivity = "7";
    CHECK(save_config_if_changed(path, error));
    CHECK(read_all(path).find("mouse_sensitivity = 7\n") != std::string::npos);

    settings.mouse_sensitivity = "3";
    CHECK(load_config(path, error));          // read back from the file
    CHECK(settings.mouse_sensitivity == "7");
    settings.mouse_sensitivity = before;

    std::ofstream(path) << "tab_size = x\n";
    CHECK_FALSE(load_config(path, error));    // reported, not fatal
    CHECK(error.find("tab_size") != std::string::npos);
}
