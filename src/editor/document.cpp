#include "editor/document.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>

#include "core/encoding.hpp"

namespace {

    // the undo history of one document: at most this many steps and bytes.
    // The oldest steps go first; the latest one always stays.
    constexpr std::size_t max_undo_steps = 2000;
    constexpr std::size_t max_history_bytes = 64 * 1024 * 1024;
    constexpr std::size_t change_overhead = 64;  // a rough cost of a change besides its text
    // typing with pauses shorter than this is one undo step (a word, a line).
    constexpr auto typing_pause = std::chrono::milliseconds(800);
    // how many edits are remembered for edits_since().
    constexpr std::size_t max_logged_edits = 512;

    std::uint64_t next_document_id() {
        static std::uint64_t last = 0;
        return ++last;
    }

    std::size_t text_hash(const std::string& text) {
        return std::hash<std::string_view>{}(text);
    }

    std::size_t common_prefix(std::string_view a, std::string_view b) {
        const std::size_t limit = std::min(a.size(), b.size());
        std::size_t n = 0;
        while (n < limit && a[n] == b[n]) {
            ++n;
        }
        return n;
    }

    // the common end of `a` and `b`, at most `limit` bytes.
    std::size_t common_suffix(std::string_view a, std::string_view b, std::size_t limit) {
        std::size_t n = 0;
        while (n < limit && a[a.size() - 1 - n] == b[b.size() - 1 - n]) {
            ++n;
        }
        return n;
    }

    // where the byte position `p` is after `removed_length` bytes at `at`
    // became `inserted_length` bytes.
    std::size_t moved_position(std::size_t p, std::size_t at, std::size_t removed_length, std::size_t inserted_length) {
        if (p <= at) {
            return p;
        }
        if (p >= at + removed_length) {
            return p - removed_length + inserted_length;
        }
        return at + std::min(p - at, inserted_length);  // was inside what changed
    }

}  // namespace

Document::Document(std::string path, std::string text, bool exists_on_disk)
    : id_(next_document_id()),
      path_(std::move(path)),
      text_(std::move(text)),
      exists_on_disk_(exists_on_disk),
      binary_(is_binary_or_unsupported_text(text_)),
      saved_size_(text_.size()),
      saved_hash_(text_hash(text_)) {
    line_starts_.push_back(0);
    const char* data = text_.data();
    const std::size_t size = text_.size();
    std::size_t at = 0;
    while (at < size) {
        const void* found = std::memchr(data + at, '\n', size - at);
        if (!found) {
            break;
        }
        at = static_cast<std::size_t>(static_cast<const char*>(found) - data) + 1;
        line_starts_.push_back(at);
    }
}

std::string Document::name() const {
    return std::filesystem::path(path_).filename().string();
}

void Document::set_path(std::string path) {
    path_ = std::move(path);
}

// --- changing the text ---------------------------------------------------

void Document::apply(std::size_t at, std::size_t removed_length, std::string_view inserted) {
    const std::size_t old_end = at + removed_length;
    const std::size_t new_end = at + inserted.size();

    // the lines, before: the starts in (at, old_end] belong to line breaks
    // that are being removed.
    const std::size_t first_line = line_of(at);
    const auto lo = std::upper_bound(line_starts_.begin(), line_starts_.end(), at);
    const auto hi = std::upper_bound(lo, line_starts_.end(), old_end);
    const std::size_t lo_index = static_cast<std::size_t>(lo - line_starts_.begin());
    const std::size_t removed_lines = static_cast<std::size_t>(hi - lo);
    const std::size_t old_end_line = first_line + removed_lines;

    TextEdit edit;
    edit.start = at;
    edit.old_end = old_end;
    edit.new_end = new_end;
    edit.start_point = {first_line, at - line_starts_[first_line]};
    edit.old_end_point = {old_end_line, old_end - line_starts_[old_end_line]};

    // the starts of the lines `inserted` brings (read before the text changes:
    // `inserted` may be a part of it).
    std::vector<std::size_t> fresh;
    for (std::size_t k = 0; k < inserted.size(); ++k) {
        if (inserted[k] == '\n') {
            fresh.push_back(at + k + 1);
        }
    }
    const std::size_t inserted_length = inserted.size();

    text_.replace(at, removed_length, inserted);

    // the line index: only what the edit touched is rewritten, the starts
    // after it just move.
    if (fresh.size() == removed_lines) {
        std::copy(fresh.begin(), fresh.end(), line_starts_.begin() + static_cast<std::ptrdiff_t>(lo_index));
    } else {
        const auto first = line_starts_.begin() + static_cast<std::ptrdiff_t>(lo_index);
        const auto after = line_starts_.erase(first, first + static_cast<std::ptrdiff_t>(removed_lines));
        line_starts_.insert(after, fresh.begin(), fresh.end());
    }
    if (inserted_length != removed_length) {
        for (std::size_t i = lo_index + fresh.size(); i < line_starts_.size(); ++i) {
            line_starts_[i] = line_starts_[i] - removed_length + inserted_length;
        }
    }
    const std::size_t new_end_line = first_line + fresh.size();
    edit.new_end_point = {new_end_line, new_end - line_starts_[new_end_line]};

    ++version_;
    last_change_ = std::chrono::steady_clock::now();
    edit_log_.push_back(edit);
    if (edit_log_.size() > max_logged_edits) {
        edit_log_.pop_front();
    }

    // every view follows the edit: its cursor stays on the same text, and a
    // view scrolled past the edit keeps showing the same lines.
    const auto follow = [&](std::size_t& cursor, std::optional<std::size_t>& anchor, ViewPosition& position) {
        const auto moved = [&](std::size_t p) {
            p = std::min(moved_position(p, at, removed_length, inserted_length), text_.size());
            while (p > 0 && p < text_.size() && (static_cast<unsigned char>(text_[p]) & 0xC0) == 0x80) {
                --p;  // never inside a character
            }
            return p;
        };
        cursor = moved(cursor);
        if (anchor) {
            anchor = moved(*anchor);
        }
        if (first_line < position.top_line && old_end_line < position.top_line) {
            position.top_line = position.top_line - removed_lines + fresh.size();
        }
        position.top_line = std::min(position.top_line, line_starts_.size() - 1);
    };
    follow(cursor_, anchor_, view);
    for (auto& [id, state] : parked_) {
        follow(state.cursor, state.anchor, state.view);
    }
}

std::size_t Document::step_bytes(const UndoStep& step) {
    std::size_t bytes = 0;
    for (const Change& change : step.changes) {
        bytes += change.removed.size() + change.inserted.size() + change_overhead;
    }
    return bytes;
}

void Document::trim_history() {
    while (undo_steps_.size() > max_undo_steps || (history_bytes_ > max_history_bytes && undo_steps_.size() > 1)) {
        history_bytes_ -= step_bytes(undo_steps_.front());
        undo_steps_.pop_front();
    }
}

void Document::record(std::size_t at, std::string_view removed, std::string_view inserted, bool typing) {
    const auto now = std::chrono::steady_clock::now();
    const bool join = typing && !undo_steps_.empty() && now - last_typing_ <= typing_pause;
    if (!join) {
        undo_steps_.emplace_back();
        undo_steps_.back().cursor_before = cursor_;
    }
    UndoStep& step = undo_steps_.back();

    // typing on, Backspace and Delete continue the change before them, so a
    // word typed (and corrected) is one small change, not one per key.
    bool merged = false;
    if (join && !step.changes.empty()) {
        Change& last = step.changes.back();
        if (removed.empty() && at == last.at + last.inserted.size()) {
            last.inserted += inserted;  // typed on
            history_bytes_ += inserted.size();
            merged = true;
        } else if (inserted.empty() && !removed.empty() && removed.size() <= last.inserted.size() &&
                   at + removed.size() == last.at + last.inserted.size()) {
            last.inserted.resize(last.inserted.size() - removed.size());  // Backspace over what was just typed
            history_bytes_ -= removed.size();
            merged = true;
        } else if (inserted.empty() && last.inserted.empty() && at + removed.size() == last.at) {
            last.removed.insert(0, removed);  // Backspace
            last.at = at;
            history_bytes_ += removed.size();
            merged = true;
        } else if (inserted.empty() && last.inserted.empty() && at == last.at) {
            last.removed += removed;  // Delete
            history_bytes_ += removed.size();
            merged = true;
        }
    }
    if (!merged) {
        step.changes.push_back({at, std::string(removed), std::string(inserted)});
        history_bytes_ += removed.size() + inserted.size() + change_overhead;
    }

    // after a command, the next typed key starts a new step.
    last_typing_ = typing ? now : std::chrono::steady_clock::time_point{};
    for (const UndoStep& undone : redo_steps_) {
        history_bytes_ -= step_bytes(undone);
    }
    redo_steps_.clear();
    trim_history();
}

void Document::replace(std::size_t begin, std::size_t end, std::string_view with, bool typing) {
    begin = std::min(begin, text_.size());
    end = std::clamp(end, begin, text_.size());

    // only what really differs is changed and remembered: replacing a line by
    // itself with one more space is an edit of one byte.
    const std::string_view old(text_.data() + begin, end - begin);
    const std::size_t prefix = common_prefix(old, with);
    const std::size_t suffix = common_suffix(old, with, std::min(old.size(), with.size()) - prefix);
    const std::size_t at = begin + prefix;
    const std::string_view removed = old.substr(prefix, old.size() - prefix - suffix);
    const std::string_view inserted = with.substr(prefix, with.size() - prefix - suffix);

    const std::size_t cursor_after = begin + with.size();
    if (!removed.empty() || !inserted.empty()) {
        record(at, removed, inserted, typing);
        apply(at, removed.size(), inserted);
        undo_steps_.back().cursor_after = cursor_after;
    }
    cursor_ = cursor_after;
    anchor_.reset();
}

// --- saving --------------------------------------------------------------

bool Document::unsaved() const {
    if (version_ == saved_version_) {
        return false;
    }
    if (unsaved_version_ != version_) {
        // the same size is rare while editing, so the hash is rarely needed.
        unsaved_ = text_.size() != saved_size_ || text_hash(text_) != saved_hash_;
        unsaved_version_ = version_;
    }
    return unsaved_;
}

void Document::mark_saved() {
    saved_size_ = text_.size();
    saved_hash_ = text_hash(text_);
    saved_version_ = version_;
    unsaved_version_ = UINT64_MAX;
    exists_on_disk_ = true;
}

void Document::reload(std::string_view disk_text) {
    const std::string_view old(text_);
    const std::size_t prefix = common_prefix(old, disk_text);
    const std::size_t suffix = common_suffix(old, disk_text, std::min(old.size(), disk_text.size()) - prefix);
    const std::string_view removed = old.substr(prefix, old.size() - prefix - suffix);
    const std::string_view inserted = disk_text.substr(prefix, disk_text.size() - prefix - suffix);
    if (!removed.empty() || !inserted.empty()) {
        record(prefix, removed, inserted, false);
        apply(prefix, removed.size(), inserted);
        undo_steps_.back().cursor_after = cursor_;
        binary_ = is_binary_or_unsupported_text(text_);
    }
    mark_saved();
}

bool Document::changed_on_disk() const {
    return disk_stamp(path_) != disk;
}

// --- cursor, selection, views --------------------------------------------

void Document::set_cursor(std::size_t at, bool extend) {
    at = std::min(at, text_.size());
    if (extend) {
        if (!anchor_) {
            anchor_ = cursor_;
        }
    } else {
        anchor_.reset();
    }
    cursor_ = at;
}

std::pair<std::size_t, std::size_t> Document::selection() const {
    if (!anchor_) {
        return {cursor_, cursor_};
    }
    return {std::min(*anchor_, cursor_), std::max(*anchor_, cursor_)};
}

std::string Document::selected_text() const {
    const auto [begin, end] = selection();
    return text_.substr(begin, end - begin);
}

void Document::select(std::size_t anchor, std::size_t cursor) {
    anchor_ = std::min(anchor, text_.size());
    cursor_ = std::min(cursor, text_.size());
}

void Document::use_view(int id) {
    if (id == current_view_) {
        return;
    }
    ViewState mine{cursor_, anchor_, view};
    const auto found = std::find_if(parked_.begin(), parked_.end(), [id](const auto& entry) { return entry.first == id; });
    if (found == parked_.end()) {
        // a window showing this document for the first time: it starts where
        // the current one is.
        parked_.emplace_back(current_view_, std::move(mine));
    } else {
        cursor_ = found->second.cursor;
        anchor_ = found->second.anchor;
        view = found->second.view;
        *found = {current_view_, std::move(mine)};
    }
    current_view_ = id;
}

void Document::drop_view(int id) {
    parked_.erase(std::remove_if(parked_.begin(), parked_.end(), [id](const auto& entry) { return entry.first == id; }),
                  parked_.end());
}

// --- lines ---------------------------------------------------------------

std::size_t Document::line_of(std::size_t byte) const {
    const auto after = std::upper_bound(line_starts_.begin(), line_starts_.end(), byte);
    return static_cast<std::size_t>(after - line_starts_.begin()) - 1;
}

std::size_t Document::line_start(std::size_t line) const {
    return line < line_starts_.size() ? line_starts_[line] : text_.size();
}

std::size_t Document::line_content_end(std::size_t line) const {
    if (line + 1 >= line_starts_.size()) {
        return text_.size();
    }
    std::size_t end = line_starts_[line + 1] - 1;  // the '\n'
    if (end > line_starts_[line] && text_[end - 1] == '\r') {
        --end;
    }
    return end;
}

std::string_view Document::line_break() const {
    if (line_starts_.size() > 1 && line_starts_[1] >= 2 && text_[line_starts_[1] - 2] == '\r') {
        return "\r\n";
    }
    return "\n";
}

// --- undo ----------------------------------------------------------------

bool Document::undo() {
    if (undo_steps_.empty()) {
        return false;
    }
    UndoStep step = std::move(undo_steps_.back());
    undo_steps_.pop_back();
    for (auto change = step.changes.rbegin(); change != step.changes.rend(); ++change) {
        apply(change->at, change->inserted.size(), change->removed);
    }
    cursor_ = std::min(step.cursor_before, text_.size());
    anchor_.reset();
    redo_steps_.push_back(std::move(step));
    last_typing_ = {};  // the next edit starts a new step
    return true;
}

bool Document::redo() {
    if (redo_steps_.empty()) {
        return false;
    }
    UndoStep step = std::move(redo_steps_.back());
    redo_steps_.pop_back();
    for (const Change& change : step.changes) {
        apply(change.at, change.removed.size(), change.inserted);
    }
    cursor_ = std::min(step.cursor_after, text_.size());
    anchor_.reset();
    undo_steps_.push_back(std::move(step));
    last_typing_ = {};
    return true;
}

bool Document::edits_since(std::uint64_t version, std::vector<TextEdit>& out) const {
    out.clear();
    if (version > version_ || version_ - version > edit_log_.size()) {
        return false;
    }
    out.assign(edit_log_.end() - static_cast<std::ptrdiff_t>(version_ - version), edit_log_.end());
    return true;
}
