#include "ui/tab_bar.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "editor/commands.hpp"
#include "editor/settings.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"
#include "syntax/theme.hpp"

namespace fs = std::filesystem;

namespace {

    // the name shown on a tab: the file name, with its folder in front when
    // another open file has the same name.
    std::string tab_label(const Document& document) {
        int same_name = 0;
        for (const auto& other : workspace.documents()) {
            same_name += other->name() == document.name() ? 1 : 0;
        }
        if (same_name > 1) {
            const std::string folder = fs::path(document.path()).parent_path().filename().string();
            if (!folder.empty() && folder != ".") {
                return folder + "/" + document.name();
            }
        }
        return document.name();
    }

    class TabBar : public ftxui::ComponentBase {
    public:
        ftxui::Element OnRender() override {
            const auto& documents = workspace.documents();
            name_boxes_.resize(documents.size());
            close_boxes_.resize(documents.size());
            ftxui::Elements tabs;
            for (std::size_t i = 0; i < documents.size(); ++i) {
                const Document& document = *documents[i];
                const bool shown = &document == workspace.active();
                ftxui::Element name = ftxui::text(" " + tab_label(document) + (document.unsaved() ? "*" : "") + " ");
                ftxui::Element close = ftxui::text("x ");
                // white; bold when shown; an unsaved file in yellow, like the explorer.
                if (document.unsaved()) {
                    name = ftxui::color(ftxui::Color::Yellow1, name);
                }
                if (shown) {
                    name = ftxui::bold(name);
                    close = ftxui::bold(close);
                }
                ftxui::Element tab = ftxui::hbox({name | ftxui::reflect(name_boxes_[i]), close | ftxui::reflect(close_boxes_[i])});
                if (shown) {
                    tab = tab | ftxui::focus;  // kept in view when they don't all fit
                }
                tabs.push_back(tab);
                tabs.push_back(ftxui::dim(ftxui::text("│")));
            }
            return ftxui::hbox({ftxui::text(" "), ftxui::hbox(std::move(tabs)) | ftxui::xframe | ftxui::flex}) | ftxui::reflect(box_);
        }

        bool OnEvent(ftxui::Event event) override {
            if (!event.is_mouse()) {
                return false;
            }
            const ftxui::Mouse& mouse = event.mouse();
            if (mouse.motion != ftxui::Mouse::Pressed || !box_.Contain(mouse.x, mouse.y) ||
                (mouse.button != ftxui::Mouse::Left && mouse.button != ftxui::Mouse::Middle)) {
                return false;
            }
            const auto& documents = workspace.documents();
            for (std::size_t i = 0; i < documents.size() && i < name_boxes_.size(); ++i) {
                const bool on_name = name_boxes_[i].Contain(mouse.x, mouse.y);
                const bool on_close = close_boxes_[i].Contain(mouse.x, mouse.y);
                if (!on_name && !on_close) {
                    continue;
                }
                workspace.activate(documents[i].get());
                if (on_close || mouse.button == ftxui::Mouse::Middle) {
                    const CommandResult result = close_active_file();
                    (result.ok ? command_output : command_error) = result.message;
                } else {
                    call(hooks.show_editor);
                }
                return true;
            }
            return false;
        }

    private:
        ftxui::Box box_;
        std::vector<ftxui::Box> name_boxes_;
        std::vector<ftxui::Box> close_boxes_;
    };

}  // namespace

ftxui::Component make_tab_bar() {
    return std::make_shared<TabBar>();
}
