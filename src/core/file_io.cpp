#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unordered_map>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/file_io.hpp"

namespace fs = std::filesystem;

namespace {

    IoResult io_fail(const fs::path& path, int error_number) {
        return {false, path.string() + ": " + std::strerror(error_number)};
    }

    bool write_all(int fd, std::string_view data) {
        while (!data.empty()) {
            const ssize_t written = ::write(fd, data.data(), data.size());
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            data.remove_prefix(static_cast<std::size_t>(written));
        }
        return true;
    }

    struct DiskStamp {
        fs::file_time_type mtime;
        std::uintmax_t size;
    };

    std::unordered_map<std::string, DiskStamp> disk_stamps;

}  // namespace

IoResult save_atomic(const fs::path& target, std::string_view data) {
    std::error_code ec;
    fs::path real = fs::weakly_canonical(target, ec);  // keep a symlink a symlink
    if (ec) {
        real = target;
    }

    struct stat st{};
    const bool existed = ::stat(real.c_str(), &st) == 0;
    // rename() only needs a writable folder, so a read-only file must be refused here.
    if (existed && ::access(real.c_str(), W_OK) != 0) {
        return io_fail(target, errno);
    }

    fs::path tmp = real;
    tmp += ".itahi-tmp-" + std::to_string(::getpid());
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        return io_fail(target, errno);
    }

    const bool ok = write_all(fd, data)
                 && ::fchmod(fd, existed ? (st.st_mode & 07777) : 0644) == 0
                 && ::fsync(fd) == 0;
    const int write_errno = errno;
    ::close(fd);
    if (!ok) {
        ::unlink(tmp.c_str());  // the original is untouched
        return io_fail(target, write_errno);
    }
    if (::rename(tmp.c_str(), real.c_str()) != 0) {
        const int rename_errno = errno;
        ::unlink(tmp.c_str());
        return io_fail(target, rename_errno);
    }
    return {true, ""};
}

std::optional<std::string> read_text_file(const fs::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = path.string() + ": " + std::strerror(errno);
        return std::nullopt;
    }

    // read by the known size: no second copy of the file in a stringstream.
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    std::string text;
    if (!ec) {
        text.resize(static_cast<std::size_t>(size));
    }
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (in.bad()) {
        error = path.string() + ": read failed";
        return std::nullopt;
    }
    text.resize(static_cast<std::size_t>(in.gcount()));
    return text;
}

std::vector<fs::path> list_directory(const fs::path& dir, std::string& error) {
    std::vector<fs::path> entries;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        entries.push_back(it->path());
    }
    if (ec) {
        error = dir.string() + ": " + ec.message();
    }

    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    std::sort(entries.begin(), entries.end(), [&](const fs::path& a, const fs::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    return entries;
}

void remember_disk_stamp(const std::string& path) {
    std::error_code time_error, size_error;
    const auto mtime = fs::last_write_time(path, time_error);
    const auto size = fs::file_size(path, size_error);
    if (!time_error && !size_error) {
        disk_stamps[path] = {mtime, size};
    }
}

bool disk_changed_since_read(const std::string& path) {
    const auto found = disk_stamps.find(path);
    if (found == disk_stamps.end()) {
        return false;  // a new file: nothing on disk to compare with
    }
    std::error_code time_error, size_error;
    const auto mtime = fs::last_write_time(path, time_error);
    const auto size = fs::file_size(path, size_error);
    return time_error || size_error || mtime != found->second.mtime || size != found->second.size;
}
