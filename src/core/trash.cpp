#include "core/trash.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace fs = std::filesystem;

namespace {

    IoResult fail(const fs::path& path, int error_number) {
        return {false, path.string() + ": " + std::strerror(error_number)};
    }

    // the path as the Path= line of a .trashinfo file wants it: every byte
    // that isn't a plain letter, digit or one of "-._~/" as %XX.
    std::string percent_encoded(const std::string& path) {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (const unsigned char c : path) {
            const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                               c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
            if (plain) {
                out += static_cast<char>(c);
            } else {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return out;
    }

    std::string deletion_date() {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        char text[32];
        std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S", &local);
        return text;
    }

    // the folder at the top of the disk `path` is on (where the device changes).
    fs::path mount_top(fs::path path) {
        struct stat here{};
        if (::lstat(path.c_str(), &here) != 0) {
            return {};
        }
        while (path.has_parent_path() && path.parent_path() != path) {
            struct stat above{};
            if (::stat(path.parent_path().c_str(), &above) != 0 || above.st_dev != here.st_dev) {
                break;
            }
            path = path.parent_path();
        }
        return path;
    }

    bool make_private_folders(const fs::path& folder) {
        std::error_code ec;
        fs::create_directories(folder, ec);
        if (ec) {
            return false;
        }
        ::chmod(folder.c_str(), 0700);
        return true;
    }

    // moves `absolute` into the trash folder `trash`; `recorded` is what the
    // .trashinfo file says the path was.
    IoResult move_into(const fs::path& trash, const fs::path& absolute, const std::string& recorded, fs::path* trashed_as) {
        const fs::path files = trash / "files";
        const fs::path info = trash / "info";
        if (!make_private_folders(files) || !make_private_folders(info)) {
            return {false, trash.string() + ": the trash folder can't be made"};
        }

        // the .trashinfo file is made first, and only if it isn't there yet:
        // that reserves the name for this file.
        const std::string name = absolute.filename().string();
        std::string unique;
        int fd = -1;
        for (int attempt = 0; attempt < 1000 && fd < 0; ++attempt) {
            unique = attempt == 0 ? name : name + "." + std::to_string(attempt + 1);
            fd = ::open((info / (unique + ".trashinfo")).c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (fd < 0 && errno != EEXIST) {
                return fail(info, errno);
            }
        }
        if (fd < 0) {
            return {false, info.string() + ": no free name in the trash"};
        }
        const std::string text = "[Trash Info]\nPath=" + percent_encoded(recorded) + "\nDeletionDate=" + deletion_date() + "\n";
        const bool written = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
        ::close(fd);

        const fs::path info_file = info / (unique + ".trashinfo");
        const fs::path destination = files / unique;
        if (!written || ::rename(absolute.c_str(), destination.c_str()) != 0) {
            const int rename_errno = written ? errno : EIO;
            ::unlink(info_file.c_str());
            return fail(absolute, rename_errno);
        }
        if (trashed_as) {
            *trashed_as = destination;
        }
        return {true, ""};
    }

}  // namespace

IoResult move_to_trash(const fs::path& path, fs::path* trashed_as) {
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec).lexically_normal();
    if (ec) {
        return {false, path.string() + ": " + ec.message()};
    }
    struct stat st{};
    if (::lstat(absolute.c_str(), &st) != 0) {
        return fail(path, errno);
    }

    // the home trash, for what is on the same disk as it.
    const char* data_home = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    fs::path home_trash;
    if (data_home && *data_home == '/') {
        home_trash = fs::path(data_home) / "Trash";
    } else if (home && *home == '/') {
        home_trash = fs::path(home) / ".local" / "share" / "Trash";
    }
    IoResult result{false, path.string() + ": there is no trash folder (HOME isn't set)"};
    if (!home_trash.empty()) {
        result = move_into(home_trash, absolute, absolute.string(), trashed_as);
        if (result.ok) {
            return result;
        }
    }

    // another disk: its own trash, at its top, with the path from there.
    const fs::path top = mount_top(absolute);
    if (!top.empty() && top != absolute) {
        const fs::path relative = absolute.lexically_relative(top);
        const fs::path disk_trash = top / (".Trash-" + std::to_string(::getuid()));
        const IoResult on_disk = move_into(disk_trash, absolute, relative.string(), trashed_as);
        if (on_disk.ok) {
            return on_disk;
        }
    }
    return result;
}

IoResult delete_for_good(const fs::path& path) {
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) {
        return fail(path, errno);
    }
    // a folder only when it is empty (rmdir refuses the others): nothing
    // inside it can be lost by one command.
    const int removed = S_ISDIR(st.st_mode) ? ::rmdir(path.c_str()) : ::unlink(path.c_str());
    if (removed != 0) {
        if (errno == ENOTEMPTY || errno == EEXIST) {
            return {false, path.string() + " is a folder that isn't empty"};
        }
        return fail(path, errno);
    }
    return {true, ""};
}

IoResult rename_path(const fs::path& from, const fs::path& to) {
#if defined(__linux__) && defined(SYS_renameat2)
    // RENAME_NOREPLACE (1): the check and the rename are one step, so nothing
    // that appears in between can be overwritten.
    if (::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 1u) == 0) {
        return {true, ""};
    }
    if (errno == EEXIST) {
        return {false, to.string() + " already exists"};
    }
    if (errno != ENOSYS && errno != EINVAL) {
        return fail(errno == ENOENT ? from : to, errno);
    }
    // an old kernel or a file system without it: the two steps below.
#endif
    struct stat st{};
    if (::lstat(to.c_str(), &st) == 0) {
        return {false, to.string() + " already exists"};
    }
    if (::rename(from.c_str(), to.c_str()) != 0) {
        return fail(errno == ENOENT ? from : to, errno);
    }
    return {true, ""};
}
