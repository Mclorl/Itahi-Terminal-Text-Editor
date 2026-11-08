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
        remember_disk_stamp(key);
    }

    // the new tab goes right after the active one, like most editors.
    auto at = std::find_if(documents_.begin(), documents_.end(), [this](const auto& d) { return d.get() == active_; });
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
    if (document == active_) {
        return;
    }
    // the matches of a search belong to the file it ran in.
    search_close();
    active_ = document;
}

bool Workspace::cycle(int step) {
    if (documents_.size() < 2 || !active_) {
        return false;
    }
    const auto at = std::find_if(documents_.begin(), documents_.end(), [this](const auto& d) { return d.get() == active_; });
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
    const bool was_active = document == active_;
    if (was_active) {
        search_close();
        active_ = nullptr;
    }
    documents_.erase(at);
    if (was_active && !documents_.empty()) {
        // the tab that took its place, or the new last one.
        active_ = documents_[std::min(index, documents_.size() - 1)].get();
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

void Workspace::clear() {
    search_close();
    documents_.clear();
    active_ = nullptr;
    opened_folders.clear();
}
