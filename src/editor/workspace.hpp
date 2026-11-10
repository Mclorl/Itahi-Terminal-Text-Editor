#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "editor/document.hpp"

// a side of a window: where a new one opens, where the focus or the window
// itself moves.
enum class Side { Left, Right, Up, Down };

// the folder Itahi opened, the files open in it (in the order of their tabs)
// and the windows that show them: up to four, in one or two columns of one or
// two windows each. The active window is the one the keys go to; its
// document is the active document.
class Workspace {
public:
    Workspace();

    // the folder Itahi opened (the explorer's top). "/name" in a command
    // means a path inside it (see editor/paths.hpp).
    std::string root = ".";

    // the folders open in the explorer.
    std::unordered_set<std::string> opened_folders;

    // -- open files --
    // the document shown in the active window; nullptr when it shows none.
    Document* active() const;
    const std::vector<std::unique_ptr<Document>>& documents() const { return documents_; }

    // the open document for `path`, or nullptr.
    Document* find(const std::string& path) const;

    // shows `path` in the active window, reading it the first time it is
    // opened. A path that doesn't exist yet becomes a new, empty document
    // (created on disk by the first save). nullptr, with `error` set, when the
    // file can't be read.
    Document* open(const std::filesystem::path& path, std::string& error);

    // opens a document with this text without reading the disk (tests).
    Document& add(std::string path, std::string text);

    // shows `document` (one of documents()) in the active window.
    void activate(Document* document);

    // shows the next (+1) or previous (-1) tab in the active window, wrapping
    // around. False when fewer than two files are open.
    bool cycle(int step);

    // closes `document`, dropping its edits (check unsaved() first). Every
    // window that showed it shows the tab next to it instead.
    void close(Document* document);

    // the first document with unsaved changes (all of them, or only those
    // inside `folder`), or nullptr.
    Document* first_unsaved() const;
    bool unsaved_under(std::string folder) const;

    // `from` (a file, or a folder and everything in it) is now called `to`:
    // the open documents and the opened folders follow.
    void rename_paths(const std::string& from, const std::string& to);

    // closes everything and goes back to one window (tests).
    void clear();

    // -- windows --
    static constexpr std::size_t max_windows = 4;

    // the windows by their ids: the columns from left to right, each from top
    // to bottom.
    const std::vector<std::vector<int>>& columns() const { return columns_; }
    std::size_t window_count() const { return windows_.size(); }
    int active_window() const { return active_window_; }
    bool has_window(int window) const;
    // the document a window shows (nullptr: none, or no such window).
    Document* document_of(int window) const;
    // windows are counted from 1 in reading order: down the left column, then
    // down the right one. 0 / -1 when there is no such window.
    int window_number(int window) const;
    int window_by_number(int number) const;

    // gives a window the focus.
    void focus_window(int window);
    // the window next to the active one on that side, or -1.
    int window_at(Side side) const;
    bool focus_side(Side side);
    // the next (+1) or previous (-1) window in reading order, wrapping around.
    void focus_next(int step);

    // opens a window on that side of the active one, showing the same
    // document, and gives it the focus. With no room on that side (two columns
    // and two rows are the most) it opens where there is room and `note` says
    // where. Returns its id, or -1 with `note` set when four windows are open.
    int split(Side side, std::string& note);

    // closes a window; its file stays open. False (`error` set) when it is the
    // only window.
    bool close_window(int window, std::string& error);
    void close_other_windows();

    // moves the active window to that side: it changes places with the window
    // there, joins the other column, or (with no window there) becomes a
    // column or a row of its own. False (`error` set) when there is no room.
    bool move_window(Side side, std::string& error);

private:
    struct Window {
        int id;
        Document* document = nullptr;
    };

    Window* window(int id);
    const Window* window(int id) const;
    // (column, row) of a window; (-1, -1) when there is no such window.
    std::pair<int, int> place_of(int id) const;
    void forget_views(int id);

    std::vector<std::unique_ptr<Document>> documents_;

    std::vector<Window> windows_;
    std::vector<std::vector<int>> columns_;
    int active_window_ = 0;
    int next_window_id_ = 1;
    // the row last focused in each column, to come back to it.
    std::array<int, 2> remembered_row_{0, 0};
};

extern Workspace workspace;

// the path of the active document, "" when no file is open.
const std::string& active_path();

// makes `window` the current view of `document` while it is alive, and puts
// the view that was current back afterwards: for drawing or scrolling a
// window that isn't the active one.
class ViewScope {
public:
    ViewScope(Document& document, int window) : document_(document), previous_(document.current_view()) {
        document_.use_view(window);
    }
    ~ViewScope() { document_.use_view(previous_); }
    ViewScope(const ViewScope&) = delete;
    ViewScope& operator=(const ViewScope&) = delete;

private:
    Document& document_;
    int previous_;
};
