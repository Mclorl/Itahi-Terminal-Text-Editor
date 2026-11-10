#include "editor/recovery.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/file_io.hpp"
#include "editor/document.hpp"
#include "editor/settings.hpp"
#include "editor/workspace.hpp"

namespace fs = std::filesystem;

namespace {

    // a copy starts with this line, then "<bytes of the path>\n", the path,
    // "\n", and the text.
    constexpr std::string_view magic = "itahi-recovery 1\n";
    // a file larger than this gets no copy: writing it every few seconds
    // would get in the way of typing.
    constexpr std::size_t max_copy_bytes = 64 * 1024 * 1024;

    fs::path& folder_override() {
        static fs::path folder;
        return folder;
    }

    std::string absolute_path(const std::string& path) {
        std::error_code ec;
        const fs::path absolute = fs::absolute(path, ec);
        return (ec ? fs::path(path) : absolute).lexically_normal().string();
    }

    // FNV-1a: the same file name for the same path in every run and build.
    std::uint64_t stable_hash(std::string_view text) {
        std::uint64_t hash = 1469598103934665603ull;
        for (const unsigned char c : text) {
            hash = (hash ^ c) * 1099511628211ull;
        }
        return hash;
    }

    // "<16 hex digits of the path's hash>-<file name>.rec": the hash keeps
    // files with the same name apart, the name is for people looking in the
    // folder.
    fs::path copy_file(const std::string& absolute) {
        static const char* hex = "0123456789abcdef";
        std::uint64_t hash = stable_hash(absolute);
        std::string name(16, '0');
        for (int i = 15; i >= 0; --i) {
            name[static_cast<std::size_t>(i)] = hex[hash & 15];
            hash >>= 4;
        }
        std::string file_name = fs::path(absolute).filename().string();
        for (char& c : file_name) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u == 0x7F || c == '/') {
                c = '_';
            }
        }
        if (file_name.size() > 80) {
            file_name.resize(80);
        }
        return recovery_folder() / (name + "-" + file_name + ".rec");
    }

    // the first bytes of a file: enough for the header of a copy, whose text
    // can be large.
    std::string read_head(const fs::path& file, std::size_t most) {
        std::string head;
        const int fd = ::open(file.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
        if (fd < 0) {
            return head;
        }
        head.resize(most);
        std::size_t used = 0;
        while (used < most) {
            const ssize_t got = ::read(fd, head.data() + used, most - used);
            if (got <= 0) {
                break;
            }
            used += static_cast<std::size_t>(got);
        }
        ::close(fd);
        head.resize(used);
        return head;
    }

    // reads the header of a copy: the path it belongs to, and where its text starts.
    bool parse_header(std::string_view data, std::string& path, std::size_t& text_start) {
        if (data.substr(0, magic.size()) != magic) {
            return false;
        }
        std::size_t at = magic.size();
        std::size_t length = 0;
        std::size_t digits = 0;
        while (at < data.size() && data[at] >= '0' && data[at] <= '9' && digits < 9) {
            length = length * 10 + static_cast<std::size_t>(data[at] - '0');
            ++at;
            ++digits;
        }
        if (digits == 0 || at >= data.size() || data[at] != '\n') {
            return false;
        }
        ++at;
        if (length == 0 || length > data.size() - at || at + length >= data.size() || data[at + length] != '\n') {
            return false;
        }
        path = std::string(data.substr(at, length));
        text_start = at + length + 1;
        return true;
    }

}  // namespace

fs::path recovery_folder() {
    if (!folder_override().empty()) {
        return folder_override();
    }
    const char* state = std::getenv("XDG_STATE_HOME");
    if (state && *state == '/') {
        return fs::path(state) / "itahi" / "recovery";
    }
    const char* home = std::getenv("HOME");
    if (home && *home == '/') {
        return fs::path(home) / ".local" / "state" / "itahi" / "recovery";
    }
    return {};  // nowhere to keep them
}

void set_recovery_folder(fs::path folder) {
    folder_override() = std::move(folder);
}

std::vector<RecoveryCopy> recovery_copies() {
    std::vector<RecoveryCopy> copies;
    const fs::path folder = recovery_folder();
    if (folder.empty()) {
        return copies;
    }
    std::string list_error;
    for (const DirEntry& entry : list_directory_entries(folder, list_error)) {
        if (!entry.is_regular_file || entry.path.extension() != ".rec") {
            continue;
        }
        std::string path;
        std::size_t text_start = 0;
        if (parse_header(read_head(entry.path, 8192), path, text_start)) {
            copies.push_back({std::move(path), entry.path});
        }
    }
    std::sort(copies.begin(), copies.end(), [](const RecoveryCopy& a, const RecoveryCopy& b) { return a.path < b.path; });
    return copies;
}

std::optional<std::string> read_recovery_copy(const RecoveryCopy& copy, std::string& error) {
    std::optional<std::string> data = read_text_file(copy.file, error);
    if (!data) {
        return std::nullopt;
    }
    std::string path;
    std::size_t text_start = 0;
    if (!parse_header(*data, path, text_start)) {
        error = copy.file.string() + ": not a recovery copy";
        return std::nullopt;
    }
    data->erase(0, text_start);
    return data;
}

bool keep_recovery_copy(Document& document, std::string& error) {
    const fs::path folder = recovery_folder();
    if (folder.empty()) {
        error = "Recovery copies have nowhere to go (HOME isn't set)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) {
        error = "Could not keep a recovery copy: " + folder.string() + ": " + ec.message();
        return false;
    }
    ::chmod(folder.c_str(), 0700);  // unsaved text is nobody else's business

    const std::string absolute = absolute_path(document.path());
    std::string data;
    data.reserve(magic.size() + absolute.size() + document.text().size() + 32);
    data += magic;
    data += std::to_string(absolute.size());
    data += '\n';
    data += absolute;
    data += '\n';
    data += document.text();

    SaveOptions options;
    options.sync = false;  // rewritten every few seconds: not worth waiting for the disk
    options.new_file_mode = 0600;
    const IoResult result = save_atomic(copy_file(absolute), data, options);
    if (!result.ok) {
        error = "Could not keep a recovery copy: " + result.error;
        return false;
    }
    document.recovery_version = document.version();
    return true;
}

void drop_recovery_copy_of(const std::string& path) {
    if (recovery_folder().empty()) {
        return;
    }
    std::error_code ec;
    fs::remove(copy_file(absolute_path(path)), ec);
}

void drop_recovery_copy(Document& document) {
    drop_recovery_copy_of(document.path());
    document.recovery_version = UINT64_MAX;
}

std::string update_recovery_copies(std::chrono::milliseconds idle) {
    if (!settings.keep_recovery_copies) {
        return "";
    }
    std::string first_error;
    const auto now = std::chrono::steady_clock::now();
    for (const auto& document : workspace.documents()) {
        const bool has_copy = document->recovery_version != UINT64_MAX;
        if (!document->unsaved()) {
            if (has_copy) {
                drop_recovery_copy(*document);  // saved, or undone back to the file
            }
            continue;
        }
        if (document->recovery_version == document->version() || now - document->last_change() < idle ||
            document->text().size() > max_copy_bytes) {
            continue;  // already kept, still being typed in, or too large
        }
        std::string error;
        if (!keep_recovery_copy(*document, error) && first_error.empty()) {
            first_error = error;
            // not again on every tick: the same error would come back each second.
            document->recovery_version = document->version();
        }
    }
    return first_error;
}

int keep_all_recovery_copies() {
    if (!settings.keep_recovery_copies) {
        return 0;
    }
    int written = 0;
    for (const auto& document : workspace.documents()) {
        if (!document->unsaved() || document->text().size() > max_copy_bytes) {
            continue;
        }
        std::string error;
        if (document->recovery_version == document->version() || keep_recovery_copy(*document, error)) {
            ++written;
        }
    }
    return written;
}
