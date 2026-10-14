#pragma once

// A scratch folder for tests that need real files (database manifests). Removed on destruction.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace cigi::test {

class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::error_code ec;
        path_ = std::filesystem::temp_directory_path(ec) /
                ("cigi_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    /// Write `content` to `relative` (creating folders) and return the full path.
    std::filesystem::path write(const std::filesystem::path& relative, std::string_view content) {
        const auto full = path_ / relative;
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);
        std::ofstream out(full, std::ios::binary | std::ios::trunc);
        out << content;
        return full;
    }

    /// Move a file's modification time `seconds` forward (file systems have coarse clocks, so a
    /// rewrite alone may not change it).
    static void bump_mtime(const std::filesystem::path& file, int seconds) {
        std::error_code ec;
        const auto now = std::filesystem::last_write_time(file, ec);
        std::filesystem::last_write_time(file, now + std::chrono::seconds(seconds), ec);
    }

private:
    std::filesystem::path path_;
};

} // namespace cigi::test
