#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// one open file: its text, what is on disk, the cursor, the selection and the
// undo history. No FTXUI here: the UI draws a Document and edits it through
// replace(), the only way its text changes.

class Document {
public:
    // `saved_text` is the file as read from disk ("" for a new file).
    Document(std::string path, std::string saved_text, bool exists_on_disk);

    // the path as opened ("./src/a.cpp"): the key used by the explorer and
    // the commands. name() is the file name ("a.cpp").
    const std::string& path() const { return path_; }
    std::string name() const;
    void set_path(std::string path);  // ">w name" (save as)

    const std::string& text() const { return text_; }

    // changes every time the text changes, so views and caches can tell
    // when what they computed from the text is out of date.
    std::uint64_t version() const { return version_; }

    // replaces [begin, end) with `with` and puts the cursor after it; the
    // selection is cleared. Edits made by typing (`typing`) less than a moment
    // apart are one undo step; any other edit is a step of its own.
    void replace(std::size_t begin, std::size_t end, std::string_view with, bool typing = true);

    // replaces the whole text (a command such as >%s): one undo step. The
    // cursor stays where it was, inside the new text.
    void replace_all(std::string text);

    // -- saving --
    // true when the text differs from the file on disk.
    bool unsaved() const;
    const std::string& saved_text() const { return saved_text_; }
    bool exists_on_disk() const { return exists_on_disk_; }
    void mark_saved();  // the text was just written to disk

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

    // -- lines --
    // byte offset of the start of every line (computed again only when the
    // text changed).
    const std::vector<std::size_t>& line_starts() const;
    std::size_t line_count() const { return line_starts().size(); }
    std::size_t line_of(std::size_t byte) const;  // from 0
    std::size_t line_start(std::size_t line) const;
    // where the visible part of the line ends: at its '\n', or at the '\r' of
    // a "\r\n", or at the end of the text.
    std::size_t line_content_end(std::size_t line) const;
    // "\r\n" when the file's first line ends that way, else "\n": what Enter inserts.
    std::string_view line_break() const;

    // -- undo --
    bool undo();  // false: nothing to undo
    bool redo();  // false: nothing to redo

    // -- binary files --
    // the file as read is binary or not valid UTF-8.
    bool binary() const;
    // binary, and the user hasn't chosen "Open Anyway".
    bool hidden_as_binary() const { return !opened_anyway_ && binary(); }
    void open_anyway() { opened_anyway_ = true; }

    // where the view of this document was scrolled to, kept when another
    // file is shown and this one comes back.
    struct ViewPosition {
        std::size_t top_line = 0;  // first line on screen
        int top_row = 0;           // with word wrap: first row of that line on screen
        int left = 0;              // without word wrap: first column on screen
    };
    ViewPosition view;

private:
    struct UndoStep {
        std::string text;
        std::size_t cursor;
    };

    void text_changed();
    void push_undo_step(bool typing);

    std::string path_;
    std::string text_;
    std::string saved_text_;
    bool exists_on_disk_;
    bool opened_anyway_ = false;
    std::uint64_t version_ = 0;

    std::size_t cursor_ = 0;
    std::optional<std::size_t> anchor_;

    std::vector<UndoStep> undo_steps_;
    std::vector<UndoStep> redo_steps_;
    std::chrono::steady_clock::time_point last_typing_{};

    // caches, valid for the version they were computed for.
    mutable std::vector<std::size_t> line_starts_;
    mutable std::uint64_t line_starts_version_ = UINT64_MAX;
    mutable std::uint64_t unsaved_version_ = UINT64_MAX;
    mutable bool unsaved_ = false;
    mutable int binary_ = -1;  // -1: not checked yet
};
