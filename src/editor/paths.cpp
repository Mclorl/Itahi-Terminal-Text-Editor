#include "editor/paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#include "core/file_io.hpp"
#include "editor/state.hpp"

namespace fs = std::filesystem;

fs::path resolve_user_path(const std::string& typed) {
    if (!typed.empty() && typed[0] == '/') {
        return fs::path(workspace_root) / typed.substr(1);
    }
    if (typed == "~" || typed.rfind("~/", 0) == 0) {
        const char* home = std::getenv("HOME");
        return fs::path(home ? home : ".") / (typed.size() > 2 ? typed.substr(2) : std::string());
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

    const fs::path folder = folder_part.empty() ? fs::path(".") : resolve_user_path(folder_part);
    std::string error;
    std::vector<std::string> names;
    for (const fs::path& entry : list_directory(folder, error)) {
        const std::string name = entry.filename().string();
        if (name.rfind(prefix, 0) != 0) {
            continue;
        }
        if (name[0] == '.' && (prefix.empty() || prefix[0] != '.')) {
            continue;  // hidden, unless asked for
        }
        std::error_code ec;
        names.push_back(fs::is_directory(entry, ec) ? name + "/" : name);
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
