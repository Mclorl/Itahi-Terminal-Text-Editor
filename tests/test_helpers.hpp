#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <unistd.h>

namespace test {

    // a fresh, empty folder for one test, removed afterwards.
    struct TempDir {
        std::filesystem::path path;
        TempDir() {
            path = std::filesystem::temp_directory_path() / ("itahi-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
            std::filesystem::create_directories(path);
        }
        ~TempDir() {
            std::error_code ec;
            std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::add, ec);
            std::filesystem::remove_all(path, ec);
        }
        static inline int counter = 0;
    };

    inline std::string read_all(const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

}  // namespace test
