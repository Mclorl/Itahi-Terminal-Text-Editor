#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "../library/tab_contents.hpp"
#include "../library/custom_styling.hpp"

// global variables.
#include "../library/global_variables.hpp"

#include "../library/sets_of_tools.hpp"


ftxui::Component btn_file = create_text_button("File", 0, tab_selected, status);
ftxui::Component btn_edit = create_text_button("Edit", 1, tab_selected, status);
ftxui::Component btn_view = create_text_button("View", 2, tab_selected, status);
ftxui::Component btn_settings = create_text_button("Settings", 3, tab_selected, status);
ftxui::Component btn_help = create_text_button("Help", 4, tab_selected, status);
// ftxui::Component btn_term = create_text_button("Terminal", 3);

// menu bar container function
ftxui::Component menu_bar_container = ftxui::Container::Horizontal({
    btn_file,
    btn_edit,
    btn_view,
    // btn_term,
    btn_settings,
    btn_help,
});

ftxui::Component top_bar(std::string &active_file) {
    return ftxui::Renderer(menu_bar_container, [&active_file] {
        return ftxui::hbox({
            ftxui::flex(
                menu_bar_container->Render()
            ),
            ftxui::flex(
                ftxui::center(
                    ftxui::bold(ftxui::text(active_file.empty() ? "--" : active_file))
                )
            ),
            ftxui::flex(
                ftxui::hbox({SEPARATOR_TRANSPARENT})
            )
        });
    });
}

ftxui::Component top_bar_component = top_bar(active_file);


