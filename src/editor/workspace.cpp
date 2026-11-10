#include "editor/workspace.hpp"

#include <algorithm>
#include <optional>
#include <system_error>

#include "core/file_io.hpp"
#include "editor/search.hpp"

namespace fs = std::filesystem;

Workspace workspace;

const std::string& active_path() {
    static const std::string none;
    return workspace.active() ? workspace.active()->path() : none;
}

Workspace::Workspace() {
    windows_.push_back({0, nullptr});
    columns_.push_back({0});
}

// --- open files ----------------------------------------------------------

Document* Workspace::active() const {
    return document_of(active_window_);
}

Document* Workspace::find(const std::string& path) const {
    for (const auto& document : documents_) {
        if (document->path() == path) {
            return document.get();
        }
    }
    return nullptr;
}

Document* Workspace::open(const fs::path& path, std::string& error) {
    const std::string key = path.string();
    if (Document* open_already = find(key)) {
        activate(open_already);
        return open_already;
    }

    // read when it is opened, not when its folder is listed.
    std::error_code exists_error;
    std::unique_ptr<Document> document;
    if (!fs::exists(path, exists_error) && !exists_error) {
        document = std::make_unique<Document>(key, std::string(), false);
    } else {
        std::optional<std::string> text = read_text_file(path, error);
        if (!text) {
            return nullptr;
        }
        document = std::make_unique<Document>(key, std::move(*text), true);
    }
    document->disk = disk_stamp(path);

    // the new tab goes right after the active one, like most editors.
    Document* shown = active();
    auto at = std::find_if(documents_.begin(), documents_.end(), [shown](const auto& d) { return d.get() == shown; });
    if (at != documents_.end()) {
        ++at;
    }
    Document* added = documents_.insert(at, std::move(document))->get();
    activate(added);
    return added;
}

Document& Workspace::add(std::string path, std::string text) {
    documents_.push_back(std::make_unique<Document>(std::move(path), std::move(text), false));
    activate(documents_.back().get());
    return *documents_.back();
}

void Workspace::activate(Document* document) {
    Window* shown_in = window(active_window_);
    if (!shown_in) {
        return;
    }
    if (shown_in->document != document) {
        // the matches of a search belong to the file it ran in.
        search_close();
        shown_in->document = document;
    }
    if (document) {
        document->use_view(active_window_);
    }
}

bool Workspace::cycle(int step) {
    Document* shown = active();
    if (documents_.size() < 2 || !shown) {
        return false;
    }
    const auto at = std::find_if(documents_.begin(), documents_.end(), [shown](const auto& d) { return d.get() == shown; });
    const int count = static_cast<int>(documents_.size());
    const int index = static_cast<int>(at - documents_.begin());
    activate(documents_[static_cast<std::size_t>(((index + step) % count + count) % count)].get());
    return true;
}

void Workspace::close(Document* document) {
    const auto at = std::find_if(documents_.begin(), documents_.end(), [document](const auto& d) { return d.get() == document; });
    if (at == documents_.end()) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(at - documents_.begin());
    const bool was_active = document == active();
    if (was_active) {
        search_close();
    }
    const std::unique_ptr<Document> closing = std::move(*at);  // alive until every window let go of it
    documents_.erase(at);
    // the tab that took its place, or the new last one.
    Document* next = documents_.empty() ? nullptr : documents_[std::min(index, documents_.size() - 1)].get();
    for (Window& shown_in : windows_) {
        if (shown_in.document == document) {
            shown_in.document = next;
        }
    }
    if (was_active && next) {
        next->use_view(active_window_);
    }
}

Document* Workspace::first_unsaved() const {
    for (const auto& document : documents_) {
        if (document->unsaved()) {
            return document.get();
        }
    }
    return nullptr;
}

bool Workspace::unsaved_under(std::string folder) const {
    if (folder.empty() || folder.back() != '/') {
        folder += '/';
    }
    for (const auto& document : documents_) {
        if (document->path().compare(0, folder.size(), folder) == 0 && document->unsaved()) {
            return true;
        }
    }
    return false;
}

void Workspace::rename_paths(const std::string& from, const std::string& to) {
    const std::string inside = from + "/";
    const auto renamed = [&](const std::string& path) -> std::optional<std::string> {
        if (path == from) {
            return to;
        }
        if (path.compare(0, inside.size(), inside) == 0) {
            return to + "/" + path.substr(inside.size());
        }
        return std::nullopt;
    };
    for (const auto& document : documents_) {
        if (const auto path = renamed(document->path())) {
            document->set_path(*path);
            document->disk = disk_stamp(*path);
        }
    }
    std::unordered_set<std::string> folders;
    for (const std::string& folder : opened_folders) {
        const auto path = renamed(folder);
        folders.insert(path ? *path : folder);
    }
    opened_folders = std::move(folders);
}

void Workspace::clear() {
    search_close();
    windows_.assign(1, Window{0, nullptr});
    columns_.assign(1, std::vector<int>{0});
    active_window_ = 0;
    next_window_id_ = 1;
    remembered_row_ = {0, 0};
    documents_.clear();
    opened_folders.clear();
}

// --- windows -------------------------------------------------------------

Workspace::Window* Workspace::window(int id) {
    for (Window& candidate : windows_) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

const Workspace::Window* Workspace::window(int id) const {
    for (const Window& candidate : windows_) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

std::pair<int, int> Workspace::place_of(int id) const {
    for (std::size_t c = 0; c < columns_.size(); ++c) {
        for (std::size_t r = 0; r < columns_[c].size(); ++r) {
            if (columns_[c][r] == id) {
                return {static_cast<int>(c), static_cast<int>(r)};
            }
        }
    }
    return {-1, -1};
}

bool Workspace::has_window(int id) const {
    return window(id) != nullptr;
}

Document* Workspace::document_of(int id) const {
    const Window* found = window(id);
    return found ? found->document : nullptr;
}

int Workspace::window_number(int id) const {
    int number = 0;
    for (const auto& column : columns_) {
        for (const int candidate : column) {
            ++number;
            if (candidate == id) {
                return number;
            }
        }
    }
    return 0;
}

int Workspace::window_by_number(int number) const {
    int count = 0;
    for (const auto& column : columns_) {
        for (const int candidate : column) {
            if (++count == number) {
                return candidate;
            }
        }
    }
    return -1;
}

void Workspace::focus_window(int id) {
    const auto [column, row] = place_of(id);
    if (column < 0) {
        return;
    }
    Document* before = active();
    active_window_ = id;
    remembered_row_[static_cast<std::size_t>(column)] = row;
    Document* now = active();
    if (now) {
        now->use_view(id);
    }
    if (now != before) {
        search_close();  // the matches of a search belong to the file it ran in
    }
}

int Workspace::window_at(Side side) const {
    const auto [column, row] = place_of(active_window_);
    if (column < 0) {
        return -1;
    }
    const auto& own = columns_[static_cast<std::size_t>(column)];
    if (side == Side::Up || side == Side::Down) {
        const int target = row + (side == Side::Up ? -1 : 1);
        return target >= 0 && target < static_cast<int>(own.size()) ? own[static_cast<std::size_t>(target)] : -1;
    }
    const int target_column = column + (side == Side::Left ? -1 : 1);
    if (target_column < 0 || target_column >= static_cast<int>(columns_.size())) {
        return -1;
    }
    const auto& other = columns_[static_cast<std::size_t>(target_column)];
    // the same row; coming from a column of one window, the row that column
    // was left on.
    int target_row = row;
    if (own.size() < other.size()) {
        target_row = remembered_row_[static_cast<std::size_t>(target_column)];
    }
    target_row = std::clamp(target_row, 0, static_cast<int>(other.size()) - 1);
    return other[static_cast<std::size_t>(target_row)];
}

bool Workspace::focus_side(Side side) {
    const int target = window_at(side);
    if (target < 0) {
        return false;
    }
    focus_window(target);
    return true;
}

void Workspace::focus_next(int step) {
    const int count = static_cast<int>(window_count());
    if (count < 2) {
        return;
    }
    const int number = window_number(active_window_);
    focus_window(window_by_number(((number - 1 + step) % count + count) % count + 1));
}

int Workspace::split(Side side, std::string& note) {
    if (window_count() >= max_windows) {
        note = "Four windows are open, the most there can be (>close closes one)";
        return -1;
    }
    const auto [column, row] = place_of(active_window_);
    const std::size_t c = static_cast<std::size_t>(column);
    const int id = next_window_id_++;
    const bool sideways = side == Side::Left || side == Side::Right;

    if (sideways && columns_.size() == 1) {
        columns_.insert(columns_.begin() + (side == Side::Right ? column + 1 : column), std::vector<int>{id});
    } else if (!sideways && columns_[c].size() == 1) {
        columns_[c].insert(columns_[c].begin() + (side == Side::Down ? row + 1 : row), id);
    } else if (columns_[c].size() == 1) {
        // a third column was asked for: below the active window instead.
        columns_[c].push_back(id);
        note = "Two columns are the most: the new window is below";
    } else if (columns_.size() == 1) {
        // a third row was asked for: a new column instead.
        columns_.push_back(std::vector<int>{id});
        note = "Two rows are the most: the new window is on the right";
    } else {
        // this column is full: the other one has room.
        columns_[1 - c].push_back(id);
        note = "This column is full: the new window is in the other one";
    }
    windows_.push_back({id, active()});
    focus_window(id);
    return id;
}

void Workspace::forget_views(int id) {
    for (const auto& document : documents_) {
        document->drop_view(id);
    }
}

bool Workspace::close_window(int id, std::string& error) {
    const auto [column, row] = place_of(id);
    if (column < 0) {
        error = "No such window";
        return false;
    }
    if (window_count() == 1) {
        error = "Only one window is open";
        return false;
    }
    const std::size_t c = static_cast<std::size_t>(column);
    Document* before = active();
    columns_[c].erase(columns_[c].begin() + row);
    if (columns_[c].empty()) {
        columns_.erase(columns_.begin() + column);
    }
    windows_.erase(std::remove_if(windows_.begin(), windows_.end(), [id](const Window& w) { return w.id == id; }), windows_.end());

    if (id == active_window_) {
        // the window that was next to it: the other one of its column, else
        // the one beside it.
        const std::size_t nc = std::min(c, columns_.size() - 1);
        const std::size_t nr = std::min(static_cast<std::size_t>(row), columns_[nc].size() - 1);
        active_window_ = columns_[nc][nr];
        remembered_row_[nc] = static_cast<int>(nr);
        Document* now = active();
        if (now) {
            now->use_view(active_window_);
        }
        if (now != before) {
            search_close();
        }
    }
    forget_views(id);
    return true;
}

void Workspace::close_other_windows() {
    const Window kept = *window(active_window_);
    for (const Window& other : std::vector<Window>(windows_)) {
        if (other.id != kept.id) {
            forget_views(other.id);
        }
    }
    windows_.assign(1, kept);
    columns_.assign(1, std::vector<int>{kept.id});
    remembered_row_ = {0, 0};
}

bool Workspace::move_window(Side side, std::string& error) {
    if (window_count() < 2) {
        error = "Only one window is open";
        return false;
    }
    const auto [column, row] = place_of(active_window_);
    const std::size_t c = static_cast<std::size_t>(column);
    const std::size_t r = static_cast<std::size_t>(row);
    const int id = active_window_;
    const bool alone = columns_[c].size() == 1;

    if (side == Side::Up || side == Side::Down) {
        if (!alone) {
            if ((side == Side::Up) == (r == 0)) {
                error = side == Side::Up ? "The window is already at the top" : "The window is already at the bottom";
                return false;
            }
            std::swap(columns_[c][0], columns_[c][1]);
        } else {
            // side by side: the window goes above or below the other one.
            const std::size_t other = 1 - c;
            if (columns_[other].size() > 1) {
                error = "No room: the other column already has two windows";
                return false;
            }
            const int neighbour = columns_[other][0];
            columns_.assign(1, side == Side::Up ? std::vector<int>{id, neighbour} : std::vector<int>{neighbour, id});
        }
    } else {
        const int target = column + (side == Side::Left ? -1 : 1);
        if (target < 0 || target >= static_cast<int>(columns_.size())) {
            // no column there: one is made, when there is room for it.
            if (columns_.size() > 1 || alone) {
                error = side == Side::Left ? "The window is already on the left" : "The window is already on the right";
                return false;
            }
            columns_[c].erase(columns_[c].begin() + row);
            columns_.insert(side == Side::Left ? columns_.begin() : columns_.end(), std::vector<int>{id});
        } else {
            const std::size_t t = static_cast<std::size_t>(target);
            if (alone || columns_[t].size() > 1) {
                // it changes places with the window beside it.
                const std::size_t other_row = std::min(r, columns_[t].size() - 1);
                std::swap(columns_[c][r], columns_[t][other_row]);
            } else {
                // it joins the other column, in the same row.
                columns_[c].erase(columns_[c].begin() + row);
                columns_[t].insert(columns_[t].begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(r, 1)), id);
            }
        }
    }
    const auto [new_column, new_row] = place_of(id);
    remembered_row_[static_cast<std::size_t>(new_column)] = new_row;
    return true;
}
