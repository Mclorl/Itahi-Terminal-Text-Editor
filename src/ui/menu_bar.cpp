#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "ui/menu_bar.hpp"

#include "ui/widgets.hpp"
#include "editor/state.hpp"
#include "ui/ui_state.hpp"

ftxui::Component make_top_bar(const std::string &active_file_name) {
    ftxui::Component menu_bar_container = ftxui::Container::Horizontal({
        create_text_button("File", 0, tab_selected, status),
        create_text_button("Edit", 1, tab_selected, status),
        create_text_button("View", 2, tab_selected, status),
        create_text_button("Settings", 3, tab_selected, status),
        create_text_button("Help", 4, tab_selected, status),
    });

    return ftxui::Renderer(menu_bar_container, [menu_bar_container, &active_file_name] {
        ftxui::Element file_name = ftxui::text(active_file_name.empty() ? "--" : active_file_name);
        if (!active_file_name.empty() && active_file_name == active_file_not_saved) {
            file_name = ftxui::color(ftxui::Color::Yellow, file_name);
        }

        const char* mode_name = mode == Mode::Normal ? " -- NORMAL --"
                              : mode == Mode::Edit   ? "   -- EDIT --"
                                                     : "-- COMMAND --";
        return ftxui::hbox({
            ftxui::flex(menu_bar_container->Render()),
            ftxui::flex(ftxui::center(ftxui::bold(file_name))),
            ftxui::flex(ftxui::hbox({ftxui::separatorEmpty()})),
            ftxui::bold(ftxui::text(mode_name)),
        });
    });
}
