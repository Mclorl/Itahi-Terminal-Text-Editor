#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <system_error>
#include <utility>

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

    // closes `fd` when it goes out of scope.
    struct FdCloser {
        int fd;
        ~FdCloser() {
            if (fd >= 0) {
                ::close(fd);
            }
        }
    };

    // flushes the folder's entry for a file that was just renamed into it, so
    // the rename itself survives a power cut. Best effort.
    void sync_folder(const fs::path& file) {
        fs::path folder = file.parent_path();
        if (folder.empty()) {
            folder = ".";
        }
        const int fd = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd >= 0) {
            ::fsync(fd);
            ::close(fd);
        }
    }

    // writes over the file itself. Not atomic (a failure half way leaves a
    // damaged file), so only used when a temporary file can't replace it.
    IoResult save_in_place(const fs::path& target, const fs::path& real, std::string_view data, bool sync) {
        const int fd = ::open(real.c_str(), O_WRONLY | O_CLOEXEC | O_NOCTTY);
        if (fd < 0) {
            return io_fail(target, errno);
        }
        const FdCloser closer{fd};
        if (!write_all(fd, data) || ::ftruncate(fd, static_cast<off_t>(data.size())) != 0 || (sync && ::fsync(fd) != 0)) {
            return io_fail(target, errno);
        }
        return {true, ""};
    }

}  // namespace

IoResult save_atomic(const fs::path& target, std::string_view data, const SaveOptions& options) {
    std::error_code ec;
    fs::path real = fs::weakly_canonical(target, ec);  // keep a symlink a symlink
    if (ec) {
        real = target;
    }

    struct stat st{};
    const bool existed = ::stat(real.c_str(), &st) == 0;
    if (existed && !S_ISREG(st.st_mode)) {
        return {false, target.string() + ": not a regular file"};
    }
    // rename() only needs a writable folder, so a read-only file must be refused here.
    if (existed && ::access(real.c_str(), W_OK) != 0) {
        return io_fail(target, errno);
    }
    // a rename would cut the file's other names loose (hard links) or hand it
    // to the current user (another owner): those are written in place.
    if (existed && (st.st_nlink > 1 || st.st_uid != ::geteuid())) {
        return save_in_place(target, real, data, options.sync);
    }

    // a random name that didn't exist (mkostemp): two saves, or a leftover of
    // an editor that crashed, can never collide; created with mode 0600.
    std::string temporary = real.string() + ".itahi-tmp-XXXXXX";
    const int fd = ::mkostemp(temporary.data(), O_CLOEXEC);
    if (fd < 0) {
        const int open_errno = errno;
        // the folder can't take a new file, but the file itself is writable.
        if (existed && (open_errno == EACCES || open_errno == EPERM || open_errno == EROFS)) {
            return save_in_place(target, real, data, options.sync);
        }
        return io_fail(target, open_errno);
    }

    bool ok = write_all(fd, data);
    if (ok && existed && st.st_gid != ::getegid()) {
        // keep the file's group when allowed to (a shared project folder);
        // not being allowed is no reason to fail the save.
        if (::fchown(fd, static_cast<uid_t>(-1), st.st_gid) != 0) {
            errno = 0;
        }
    }
    ok = ok && ::fchmod(fd, existed ? (st.st_mode & 07777) : static_cast<mode_t>(options.new_file_mode & 07777)) == 0;
    ok = ok && (!options.sync || ::fsync(fd) == 0);
    const int write_errno = errno;
    ::close(fd);
    if (!ok) {
        ::unlink(temporary.c_str());  // the original is untouched
        return io_fail(target, write_errno);
    }
    if (::rename(temporary.c_str(), real.c_str()) != 0) {
        const int rename_errno = errno;
        ::unlink(temporary.c_str());
        return io_fail(target, rename_errno);
    }
    if (options.sync) {
        sync_folder(real);
    }
    return {true, ""};
}

std::optional<std::string> read_text_file(const fs::path& path, std::string& error) {
    // O_NONBLOCK: opening a pipe (FIFO) would otherwise wait for a writer
    // forever. It changes nothing for regular files.
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
    if (fd < 0) {
        error = path.string() + ": " + std::strerror(errno);
        return std::nullopt;
    }
    const FdCloser closer{fd};

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        error = path.string() + ": " + std::strerror(errno);
        return std::nullopt;
    }
    if (S_ISDIR(st.st_mode)) {
        error = path.string() + " is a folder";
        return std::nullopt;
    }
    if (!S_ISREG(st.st_mode)) {
        error = path.string() + ": not a regular file (a pipe, a device or a socket can't be edited)";
        return std::nullopt;
    }
    const auto too_large = [&path, &error] {
        error = path.string() + ": too large to edit (the limit is " + std::to_string(max_file_bytes / (1024 * 1024)) + " MB)";
        return std::nullopt;
    };
    if (static_cast<std::uintmax_t>(st.st_size) > max_file_bytes) {
        return too_large();
    }

    // the size is only where to start: files such as /proc/cpuinfo say 0 and
    // a file can grow while it is read, so read until its end.
    std::string text;
    try {
        std::size_t used = 0;
        text.resize(static_cast<std::size_t>(st.st_size) + 1);
        while (true) {
            if (used == text.size()) {
                if (used > max_file_bytes) {
                    return too_large();
                }
                text.resize(std::max<std::size_t>(text.size() * 2, 4096));
            }
            const ssize_t got = ::read(fd, text.data() + used, text.size() - used);
            if (got < 0) {
                if (errno == EINTR) {
                    continue;
                }
                error = path.string() + ": " + std::strerror(errno);
                return std::nullopt;
            }
            if (got == 0) {
                break;
            }
            used += static_cast<std::size_t>(got);
        }
        text.resize(used);
        if (text.capacity() > used + used / 8 + 4096) {
            text.shrink_to_fit();  // the file was smaller than it said
        }
    } catch (const std::bad_alloc&) {
        error = path.string() + ": not enough memory to open it";
        return std::nullopt;
    } catch (const std::length_error&) {
        return too_large();
    }
    return text;
}

std::vector<DirEntry> list_directory_entries(const fs::path& dir, std::string& error) {
    struct Keyed {
        std::string key;  // the lower-case name: computed once, not on every comparison
        DirEntry entry;
    };
    std::vector<Keyed> found;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        // the type comes with the listing; only links need a look at the file.
        std::error_code type_error;
        const fs::file_status status = it->status(type_error);
        std::string key = it->path().filename().string();
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        found.push_back({std::move(key),
                         {it->path(), !type_error && fs::is_directory(status), !type_error && fs::is_regular_file(status)}});
    }
    if (ec) {
        error = dir.string() + ": " + ec.message();
    }

    std::sort(found.begin(), found.end(), [](const Keyed& a, const Keyed& b) {
        return a.key != b.key ? a.key < b.key : a.entry.path.filename() < b.entry.path.filename();
    });
    std::vector<DirEntry> entries;
    entries.reserve(found.size());
    for (Keyed& keyed : found) {
        entries.push_back(std::move(keyed.entry));
    }
    return entries;
}

std::vector<fs::path> list_directory(const fs::path& dir, std::string& error) {
    std::vector<fs::path> paths;
    for (DirEntry& entry : list_directory_entries(dir, error)) {
        paths.push_back(std::move(entry.path));
    }
    return paths;
}

DiskStamp disk_stamp(const fs::path& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) {
        return {};
    }
    DiskStamp stamp;
    stamp.exists = true;
#if defined(__APPLE__)
    stamp.mtime_seconds = static_cast<std::int64_t>(st.st_mtimespec.tv_sec);
    stamp.mtime_nanoseconds = static_cast<std::int64_t>(st.st_mtimespec.tv_nsec);
#else
    stamp.mtime_seconds = static_cast<std::int64_t>(st.st_mtim.tv_sec);
    stamp.mtime_nanoseconds = static_cast<std::int64_t>(st.st_mtim.tv_nsec);
#endif
    stamp.size = static_cast<std::uint64_t>(st.st_size);
    stamp.inode = static_cast<std::uint64_t>(st.st_ino);
    return stamp;
}
