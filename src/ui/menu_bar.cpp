#include "ui/menu_bar.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "ui/ui_state.hpp"
#include "ui/widgets.hpp"

ftxui::Component make_top_bar() {
    ftxui::Component menu_bar_container = ftxui::Container::Horizontal({
        create_text_button("File", 0, tab_selected, status),
        create_text_button("Edit", 1, tab_selected, status),
        create_text_button("View", 2, tab_selected, status),
        create_text_button("Settings", 3, tab_selected, status),
        create_text_button("Help", 4, tab_selected, status),
    });

    return ftxui::Renderer(menu_bar_container, [menu_bar_container] {
        const Document* document = workspace.active();
        ftxui::Element file_name = ftxui::text(document ? document->name() + (document->unsaved() ? "*" : "") : "--");
        if (document && document->unsaved()) {
            file_name = ftxui::color(ftxui::Color::Yellow, file_name);
        }

        const char* mode_name = mode == Mode::Normal ? " -- NORMAL --"
                              : mode == Mode::Edit   ? "   -- EDIT --"
                                                     : "-- COMMAND --";
        return ftxui::hbox({
            ftxui::flex(ftxui::hbox({ftxui::text(" "), menu_bar_container->Render()})),
            ftxui::flex(ftxui::center(ftxui::bold(file_name))),
            ftxui::flex(ftxui::hbox({ftxui::separatorEmpty()})),
            // in the editor the status bar shows the mode; in the menus, here.
            status == 1 ? ftxui::text("") : ftxui::bold(ftxui::text(mode_name)),
        });
    });
}
