#include "platform/git.hpp"

#include <filesystem>
#include <system_error>

#include "platform/process.hpp"

namespace fs = std::filesystem;

std::string git_path_key(const std::string& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) {
        absolute = path;
    }
    std::string key = absolute.lexically_normal().string();
    // "a/b/" and "a/b" are the same folder.
    while (key.size() > 1 && key.back() == '/') {
        key.pop_back();
    }
    return key;
}

namespace {

    // the one-letter state of a porcelain "XY" pair.
    char state_letter(char x, char y) {
        if (x == '?' || x == '!') {
            return x;
        }
        if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) {
            return 'U';
        }
        // the working tree's state wins (what the editor shows), then the index's.
        for (const char c : {y, x}) {
            if (c == 'M' || c == 'A' || c == 'D' || c == 'R' || c == 'C' || c == 'T') {
                return c == 'C' ? 'A' : c == 'T' ? 'M' : c;
            }
        }
        return 'M';
    }

    // "## main...origin/main [ahead 1, behind 2]"
    void read_branch_line(std::string_view line, GitStatus& status) {
        line.remove_prefix(3);  // "## "
        if (line.rfind("No commits yet on ", 0) == 0) {
            line.remove_prefix(18);
        } else if (line.rfind("Initial commit on ", 0) == 0) {
            line.remove_prefix(18);
        }
        const std::size_t bracket = line.find(" [");
        std::string_view counts;
        if (bracket != std::string_view::npos) {
            counts = line.substr(bracket + 2);
            line = line.substr(0, bracket);
        }
        const std::size_t dots = line.find("...");
        status.branch = std::string(line.substr(0, dots));
        if (status.branch == "HEAD (no branch)") {
            status.branch = "HEAD (detached)";
        }
        auto number_after = [&](std::string_view word) {
            const std::size_t at = counts.find(word);
            if (at == std::string_view::npos) {
                return 0;
            }
            int n = 0;
            for (std::size_t i = at + word.size(); i < counts.size() && counts[i] >= '0' && counts[i] <= '9'; ++i) {
                n = n * 10 + (counts[i] - '0');
            }
            return n;
        };
        status.ahead = number_after("ahead ");
        status.behind = number_after("behind ");
    }

}  // namespace

GitStatus parse_git_status(std::string_view porcelain, const std::string& top_level) {
    GitStatus status;
    status.in_repository = true;
    const fs::path root = top_level;

    std::size_t at = 0;
    while (at < porcelain.size()) {
        std::size_t end = porcelain.find('\0', at);
        if (end == std::string_view::npos) {
            end = porcelain.size();
        }
        const std::string_view entry = porcelain.substr(at, end - at);
        at = end + 1;
        if (entry.rfind("## ", 0) == 0) {
            read_branch_line(entry, status);
            continue;
        }
        if (entry.size() < 4) {
            continue;
        }
        const char state = state_letter(entry[0], entry[1]);
        std::string relative(entry.substr(3));
        const bool folder = !relative.empty() && relative.back() == '/';
        std::string key = git_path_key((root / relative).string());
        if (folder) {
            key += '/';
        }
        status.paths[key] = state;
        // a rename is followed by its old name, which isn't shown.
        if (entry[0] == 'R' || entry[0] == 'C') {
            const std::size_t skip = porcelain.find('\0', at);
            at = skip == std::string_view::npos ? porcelain.size() : skip + 1;
        }
    }
    return status;
}

GitStatus read_git_status(const std::string& folder) {
    const ProcessResult top = run_process({"git", "-C", folder, "rev-parse", "--show-toplevel"});
    if (!top.started || top.exit_code != 0 || top.output.empty()) {
        return {};
    }
    std::string top_level = top.output;
    while (!top_level.empty() && (top_level.back() == '\n' || top_level.back() == '\r')) {
        top_level.pop_back();
    }
    const ProcessResult result = run_process(
        {"git", "-C", folder, "status", "--porcelain=v1", "-z", "--branch", "--ignored", "--untracked-files=normal"},
        "", true, 4000);
    if (!result.started || result.exit_code != 0) {
        return {};
    }
    return parse_git_status(result.output, top_level);
}

char GitStatus::state_of(const std::string& path) const {
    if (paths.empty()) {
        return ' ';
    }
    const std::string key = git_path_key(path);
    for (const std::string& exact : {key, key + "/"}) {
        const auto found = paths.find(exact);
        if (found != paths.end()) {
            return found->second;
        }
    }
    // inside a folder that is new or ignored as a whole ("build/").
    for (std::string folder = key; ;) {
        const std::size_t slash = folder.rfind('/');
        if (slash == std::string::npos || slash == 0) {
            break;
        }
        folder.resize(slash);
        const auto whole = paths.find(folder + "/");
        if (whole != paths.end()) {
            return whole->second;
        }
    }
    return ' ';
}

bool GitStatus::changes_under(const std::string& folder) const {
    const std::string key = git_path_key(folder);
    const auto from = paths.lower_bound(key + "/");
    // every path inside the folder (and the folder itself, as "folder/").
    for (auto it = from; it != paths.end() && it->first.compare(0, key.size() + 1, key + "/") == 0; ++it) {
        if (it->second != '!') {
            return true;
        }
    }
    return false;
}

bool GitStatus::ignored(const std::string& path) const {
    return state_of(path) == '!';
}

int GitStatus::changed_count() const {
    int count = 0;
    for (const auto& [path, state] : paths) {
        if (state != '!') {
            ++count;
        }
    }
    return count;
}
