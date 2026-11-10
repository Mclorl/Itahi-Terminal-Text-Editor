#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/file_io.hpp"
#include "core/text_edit.hpp"

// one open file: its text, what is on disk, the cursor, the selection and the
// undo history. No FTXUI here: the UI draws a Document and edits it through
// replace(), the only way its text changes.

class Document {
public:
    // `text` is the file as read from disk ("" for a new file).
    Document(std::string path, std::string text, bool exists_on_disk);

    // a number no other document of this run has: caches kept by path can
    // tell a file that was closed and opened again from the one they knew.
    std::uint64_t id() const { return id_; }

    // the path as opened ("./src/a.cpp"): the key used by the explorer and
    // the commands. name() is the file name ("a.cpp").
    const std::string& path() const { return path_; }
    std::string name() const;
    void set_path(std::string path);  // ">w name" (save as), ">rename"

    const std::string& text() const { return text_; }

    // +1 for every change of the text, so views and caches can tell when what
    // they computed from the text is out of date.
    std::uint64_t version() const { return version_; }

    // replaces [begin, end) with `with` and puts the cursor after it; the
    // selection is cleared. Edits made by typing (`typing`) less than a moment
    // apart are one undo step; any other edit is a step of its own.
    void replace(std::size_t begin, std::size_t end, std::string_view with, bool typing = true);

    // -- saving --
    // true when the text differs from the file on disk.
    bool unsaved() const;
    bool exists_on_disk() const { return exists_on_disk_; }
    void mark_saved();  // the text was just written to disk
    // the file changed on disk: its text replaces the document's, as one undo
    // step, and the document counts as saved. Cursors stay where they were.
    void reload(std::string_view disk_text);

    // what the file looked like on disk when it was read or last saved, and
    // whether it looks different now (another program changed it).
    DiskStamp disk;
    bool changed_on_disk() const;
    // a change on disk the user was already told about (and hasn't dealt with
    // by saving or reading the file again), so it is said only once.
    std::optional<DiskStamp> disk_seen;

    // -- cursor and selection, as byte offsets into text() --
    std::size_t cursor() const { return cursor_; }
    // moves the cursor; with `extend` the selection grows from where it was
    // (or from the old cursor), otherwise it is cleared.
    void set_cursor(std::size_t at, bool extend = false);
    bool has_selection() const { return anchor_.has_value() && *anchor_ != cursor_; }
    // [begin, end) of the selection (begin == end when there is none).
    std::pair<std::size_t, std::size_t> selection() const;
    std::string selected_text() const;
    // the selection starts at `anchor` and ends at the cursor.
    void select(std::size_t anchor, std::size_t cursor);
    void clear_selection() { anchor_.reset(); }
    // the fixed end of the selection, if there is one.
    std::optional<std::size_t> anchor() const { return anchor_; }

    // -- views --
    // every window showing this document has its own cursor, selection and
    // scroll position. The ones above are those of the current view;
    // use_view() switches to another window's (a window seen for the first
    // time starts where the current one is). Every view follows the edits.
    int current_view() const { return current_view_; }
    void use_view(int id);
    void drop_view(int id);  // that window is gone, or shows another file for good

    // -- lines --
    // byte offset of the start of every line, kept up to date on every edit.
    const std::vector<std::size_t>& line_starts() const { return line_starts_; }
    std::size_t line_count() const { return line_starts_.size(); }
    std::size_t line_of(std::size_t byte) const;  // from 0
    std::size_t line_start(std::size_t line) const;
    // where the visible part of the line ends: at its '\n', or at the '\r' of
    // a "\r\n", or at the end of the text.
    std::size_t line_content_end(std::size_t line) const;
    // "\r\n" when the file's first line ends that way, else "\n": what Enter inserts.
    std::string_view line_break() const;

    // -- undo --
    // a step keeps only what an edit removed and inserted, never the whole text.
    bool undo();  // false: nothing to undo
    bool redo();  // false: nothing to redo
    std::size_t undo_steps() const { return undo_steps_.size(); }
    std::size_t history_bytes() const { return history_bytes_; }  // of undo and redo together

    // -- what changed --
    // every edit made since `version`, oldest first, for whoever keeps
    // something in step with the text (the syntax tree). False when the
    // document doesn't remember that far back.
    bool edits_since(std::uint64_t version, std::vector<TextEdit>& out) const;
    // when the text last changed.
    std::chrono::steady_clock::time_point last_change() const { return last_change_; }

    // -- binary files --
    // the file as read is binary or not valid UTF-8.
    bool binary() const { return binary_; }
    // binary, and the user hasn't chosen "Open Anyway".
    bool hidden_as_binary() const { return !opened_anyway_ && binary_; }
    void open_anyway() { opened_anyway_ = true; }

    // the version kept in the recovery copy of this document (see
    // editor/recovery.hpp); UINT64_MAX when there is none.
    std::uint64_t recovery_version = UINT64_MAX;

    // where the view of this document was scrolled to, kept when another
    // file is shown and this one comes back.
    struct ViewPosition {
        std::size_t top_line = 0;  // first line on screen
        int top_row = 0;           // with word wrap: first row of that line on screen
        int left = 0;              // without word wrap: first column on screen
    };
    ViewPosition view;

private:
    // one replacement: at `at`, `removed` gave way to `inserted`.
    struct Change {
        std::size_t at;
        std::string removed;
        std::string inserted;
    };
    struct UndoStep {
        std::vector<Change> changes;
        std::size_t cursor_before = 0;
        std::size_t cursor_after = 0;
    };
    struct ViewState {
        std::size_t cursor = 0;
        std::optional<std::size_t> anchor;
        ViewPosition view;
    };

    // the one place the text changes: replaces `removed_length` bytes at `at`
    // with `inserted`, and updates the line index, the views and the edit log.
    void apply(std::size_t at, std::size_t removed_length, std::string_view inserted);
    void record(std::size_t at, std::string_view removed, std::string_view inserted, bool typing);
    void trim_history();
    static std::size_t step_bytes(const UndoStep& step);

    std::uint64_t id_;
    std::string path_;
    std::string text_;
    bool exists_on_disk_;
    bool binary_;
    bool opened_anyway_ = false;
    std::uint64_t version_ = 0;

    // the text as saved: its size and hash instead of a second copy of it.
    std::size_t saved_size_ = 0;
    std::size_t saved_hash_ = 0;
    std::uint64_t saved_version_ = 0;
    mutable std::uint64_t unsaved_version_ = UINT64_MAX;
    mutable bool unsaved_ = false;

    std::size_t cursor_ = 0;
    std::optional<std::size_t> anchor_;
    int current_view_ = 0;
    std::vector<std::pair<int, ViewState>> parked_;  // the views of the other windows

    std::vector<std::size_t> line_starts_;

    std::deque<UndoStep> undo_steps_;
    std::vector<UndoStep> redo_steps_;
    std::size_t history_bytes_ = 0;
    std::chrono::steady_clock::time_point last_typing_{};
    std::chrono::steady_clock::time_point last_change_{};

    std::deque<TextEdit> edit_log_;  // the latest edits, one per version
};
