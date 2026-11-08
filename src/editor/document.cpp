#include "editor/document.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "core/encoding.hpp"

namespace {

    constexpr std::size_t max_undo_steps = 200;
    // typing with pauses shorter than this is one undo step (a word, a line).
    constexpr auto typing_pause = std::chrono::milliseconds(800);

}  // namespace

Document::Document(std::string path, std::string saved_text, bool exists_on_disk)
    : path_(std::move(path)), text_(saved_text), saved_text_(std::move(saved_text)), exists_on_disk_(exists_on_disk) {}

std::string Document::name() const {
    return std::filesystem::path(path_).filename().string();
}

void Document::set_path(std::string path) {
    path_ = std::move(path);
}

void Document::text_changed() {
    ++version_;
    cursor_ = std::min(cursor_, text_.size());
    if (anchor_) {
        anchor_ = std::min(*anchor_, text_.size());
    }
}

void Document::push_undo_step(bool typing) {
    const auto now = std::chrono::steady_clock::now();
    if (!typing || undo_steps_.empty() || now - last_typing_ > typing_pause) {
        undo_steps_.push_back({text_, cursor_});
        if (undo_steps_.size() > max_undo_steps) {
            undo_steps_.erase(undo_steps_.begin());
        }
    }
    // after a command, the next typed key starts a new step.
    last_typing_ = typing ? now : std::chrono::steady_clock::time_point{};
    redo_steps_.clear();
}

void Document::replace(std::size_t begin, std::size_t end, std::string_view with, bool typing) {
    begin = std::min(begin, text_.size());
    end = std::clamp(end, begin, text_.size());
    push_undo_step(typing);
    text_.replace(begin, end - begin, with);
    cursor_ = begin + with.size();
    anchor_.reset();
    text_changed();
}

void Document::replace_all(std::string text) {
    push_undo_step(false);
    text_ = std::move(text);
    anchor_.reset();
    text_changed();
}

bool Document::unsaved() const {
    if (unsaved_version_ != version_) {
        unsaved_ = text_ != saved_text_;
        unsaved_version_ = version_;
    }
    return unsaved_;
}

void Document::mark_saved() {
    saved_text_ = text_;
    exists_on_disk_ = true;
    unsaved_version_ = UINT64_MAX;
}

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

const std::vector<std::size_t>& Document::line_starts() const {
    if (line_starts_version_ != version_) {
        line_starts_.clear();
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
        line_starts_version_ = version_;
    }
    return line_starts_;
}

std::size_t Document::line_of(std::size_t byte) const {
    const std::vector<std::size_t>& starts = line_starts();
    const auto after = std::upper_bound(starts.begin(), starts.end(), byte);
    return static_cast<std::size_t>(after - starts.begin()) - 1;
}

std::size_t Document::line_start(std::size_t line) const {
    const std::vector<std::size_t>& starts = line_starts();
    return line < starts.size() ? starts[line] : text_.size();
}

std::size_t Document::line_content_end(std::size_t line) const {
    const std::vector<std::size_t>& starts = line_starts();
    if (line + 1 >= starts.size()) {
        return text_.size();
    }
    std::size_t end = starts[line + 1] - 1;  // the '\n'
    if (end > starts[line] && text_[end - 1] == '\r') {
        --end;
    }
    return end;
}

std::string_view Document::line_break() const {
    const std::size_t newline = text_.find('\n');
    if (newline != std::string::npos && newline > 0 && text_[newline - 1] == '\r') {
        return "\r\n";
    }
    return "\n";
}

bool Document::undo() {
    if (undo_steps_.empty()) {
        return false;
    }
    redo_steps_.push_back({std::move(text_), cursor_});
    text_ = std::move(undo_steps_.back().text);
    cursor_ = undo_steps_.back().cursor;
    undo_steps_.pop_back();
    last_typing_ = {};  // the next edit starts a new step
    anchor_.reset();
    text_changed();
    return true;
}

bool Document::redo() {
    if (redo_steps_.empty()) {
        return false;
    }
    undo_steps_.push_back({std::move(text_), cursor_});
    text_ = std::move(redo_steps_.back().text);
    cursor_ = redo_steps_.back().cursor;
    redo_steps_.pop_back();
    last_typing_ = {};
    anchor_.reset();
    text_changed();
    return true;
}

bool Document::binary() const {
    if (binary_ < 0) {
        binary_ = is_binary_or_unsupported_text(saved_text_) ? 1 : 0;
    }
    return binary_ == 1;
}
