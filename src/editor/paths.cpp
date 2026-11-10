#include "editor/paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#include "core/file_io.hpp"
#include "editor/state.hpp"
#include "editor/workspace.hpp"

namespace fs = std::filesystem;

fs::path current_new_file_folder() {
    const std::string chosen = hooks.active_folder ? hooks.active_folder() : std::string();
    fs::path folder = chosen.empty() ? fs::path(workspace.root) : fs::path(chosen);
    // a folder deleted by another program must not be offered (and silently
    // made again by the next new file): the nearest one above it that is
    // still there, down to the opened folder.
    const fs::path root = fs::path(workspace.root).lexically_normal();
    std::error_code ec;
    while (!fs::is_directory(folder, ec)) {
        const fs::path above = folder.parent_path();
        if (above.empty() || above == folder || folder.lexically_normal() == root) {
            return fs::path(workspace.root);
        }
        folder = above;
    }
    return folder;
}

std::string absolute_normal(const fs::path& path) {
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    std::string text = (ec ? path : absolute).lexically_normal().string();
    while (text.size() > 1 && text.back() == '/') {
        text.pop_back();
    }
    return text;
}

fs::path explorer_form(const fs::path& file) {
    // compared as absolute paths, so "src/a.cpp", "./src/a.cpp" and
    // "/home/me/project/src/a.cpp" are all the explorer's "./src/a.cpp".
    std::error_code ec;
    const fs::path root = fs::absolute(workspace.root, ec).lexically_normal();
    const fs::path absolute = ec ? fs::path() : fs::absolute(file, ec).lexically_normal();
    if (ec || absolute.empty()) {
        return file;
    }
    const fs::path relative = absolute.lexically_relative(root);
    if (relative.empty() || *relative.begin() == "..") {
        return file;  // outside the opened folder
    }
    if (relative == ".") {
        return fs::path(workspace.root);
    }
    return fs::path(workspace.root) / relative;
}

std::string active_folder_for_command() {
    std::string folder = current_new_file_folder().lexically_normal().string();
    if (folder == "." || folder == "./" || folder.empty()) {
        return "";
    }
    if (folder.rfind("./", 0) == 0) {
        folder.erase(0, 2);
    }
    if (folder.back() != '/') {
        folder += '/';
    }
    return folder;
}

fs::path resolve_user_path(const std::string& typed) {
    if (!typed.empty() && typed[0] == '/') {
        return fs::path(workspace.root) / typed.substr(1);
    }
    if (typed == "~" || typed.rfind("~/", 0) == 0) {
        const char* home = std::getenv("HOME");
        return fs::path(home ? home : ".") / (typed.size() > 2 ? typed.substr(2) : std::string());
    }
    // a plain name ("notes.txt"): in the folder Itahi opened. To put it in
    // the active folder instead, take the ghost hint with Tab (">e src/").
    if (typed.find('/') == std::string::npos && typed != "." && typed != "..") {
        return fs::path(workspace.root) / typed;
    }
    return fs::path(typed);
}

bool create_parent_folders(const fs::path& file, std::string& error) {
    const fs::path parent = file.parent_path();
    if (parent.empty()) {
        return true;
    }
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec) {
        error = parent.string() + ": " + ec.message();
        return false;
    }
    return true;
}

PathCompletion complete_path(const std::string& typed) {
    // split into the folder part (kept as typed) and the name being completed.
    const std::size_t slash = typed.rfind('/');
    const std::string folder_part = slash == std::string::npos ? "" : typed.substr(0, slash + 1);
    const std::string prefix = slash == std::string::npos ? typed : typed.substr(slash + 1);

    const fs::path folder = folder_part.empty() ? fs::path(workspace.root) : resolve_user_path(folder_part);
    std::string error;
    std::vector<std::string> names;
    for (const DirEntry& entry : list_directory_entries(folder, error)) {
        const std::string name = entry.path.filename().string();
        if (name.rfind(prefix, 0) != 0) {
            continue;
        }
        if (name[0] == '.' && (prefix.empty() || prefix[0] != '.')) {
            continue;  // hidden, unless asked for
        }
        names.push_back(entry.is_directory ? name + "/" : name);
    }

    PathCompletion result{typed, {}};
    if (names.empty()) {
        return result;
    }
    if (names.size() == 1) {
        result.text = folder_part + names[0];
        return result;
    }

    // several: complete what they all share, and list them.
    std::string common = names[0];
    for (const std::string& name : names) {
        std::size_t n = 0;
        while (n < common.size() && n < name.size() && common[n] == name[n]) {
            n++;
        }
        common.resize(n);
    }
    result.text = folder_part + std::max(common, prefix, [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
    result.options = std::move(names);
    return result;
}
