#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "editor/document.hpp"

// the folder Itahi opened and the files open in it, in the order of their
// tabs. One of them, the active document, is shown in the editor.
class Workspace {
public:
    // the folder Itahi opened (the explorer's top). "/name" in a command
    // means a path inside it (see editor/paths.hpp).
    std::string root = ".";

    // the folders open in the explorer.
    std::unordered_set<std::string> opened_folders;

    // the document shown in the editor; nullptr when no file is open.
    Document* active() const { return active_; }
    const std::vector<std::unique_ptr<Document>>& documents() const { return documents_; }

    // the open document for `path`, or nullptr.
    Document* find(const std::string& path) const;

    // shows `path`, reading it the first time it is opened. A path that
    // doesn't exist yet becomes a new, empty document (created on disk by the
    // first save). nullptr, with `error` set, when the file can't be read.
    Document* open(const std::filesystem::path& path, std::string& error);

    // opens a document with this text without reading the disk (tests).
    Document& add(std::string path, std::string text);

    // shows `document` (one of documents()).
    void activate(Document* document);

    // shows the next (+1) or previous (-1) tab, wrapping around. False when
    // fewer than two files are open.
    bool cycle(int step);

    // closes `document`, dropping its edits (check unsaved() first). The tab
    // next to it becomes the active one.
    void close(Document* document);

    // the first document with unsaved changes (all of them, or only those
    // inside `folder`), or nullptr.
    Document* first_unsaved() const;
    bool unsaved_under(std::string folder) const;

    // closes everything (tests).
    void clear();

private:
    std::vector<std::unique_ptr<Document>> documents_;
    Document* active_ = nullptr;
};

extern Workspace workspace;

// the path of the active document, "" when no file is open.
const std::string& active_path();
