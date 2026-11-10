#pragma once

#include <map>
#include <memory>

#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/screen/box.hpp>

class EditorView;
class WindowView;

// the windows of the editor on screen (see Workspace): one or two columns of
// one or two windows each, a line between them that the mouse can drag. Each
// window draws its own document; a click gives a window the focus, the keys
// go to the active one.
class WindowGrid : public ftxui::ComponentBase {
public:
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }
    ftxui::Component ActiveChild() override;
    void SetActiveChild(ftxui::ComponentBase* child) override;

    // the text area of the active window.
    EditorView& active_editor();
    // a mouse wheel notch at a screen position: the window under it scrolls
    // (the active one when it is over none).
    void wheel(int x, int y, int rows);
    // every window the same size again.
    void equalize();
    // makes the active window `columns` wider and `rows` taller (or smaller).
    void resize_active(int columns, int rows);

private:
    void sync();  // a view for every window there is now

    std::map<int, std::shared_ptr<WindowView>> views_;
    // how much of the width the left column takes, and of each column's
    // height its top window.
    float column_ratio_ = 0.5f;
    float row_ratio_[2] = {0.5f, 0.5f};
    ftxui::Box box_;
    ftxui::Box column_box_[2];
    ftxui::Box column_separator_;
    ftxui::Box row_separator_[2];
    int dragging_ = -1;  // -1: nothing, 0: the line between the columns, 1 / 2: the line in a column
    ftxui::CapturedMouse captured_mouse_;
};

std::shared_ptr<WindowGrid> make_window_grid();
