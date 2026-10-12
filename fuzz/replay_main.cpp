// Corpus-replay driver: runs LLVMFuzzerTestOneInput over every file (directories are walked) named
// on the command line. Used by the ctests in normal builds, where libFuzzer is not linked.

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <print>
#include <span>
#include <system_error>
#include <vector>

// The libFuzzer entry-point name is fixed by libFuzzer.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

bool run_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::println(stderr, "cannot read {}", path.string());
        return false;
    }
    const std::vector<char> raw{std::istreambuf_iterator<char>(in), {}};
    std::vector<std::uint8_t> bytes;
    bytes.reserve(raw.size());
    for (const char c : raw) {
        bytes.push_back(static_cast<std::uint8_t>(c));
    }
    (void)LLVMFuzzerTestOneInput(bytes.data(),
                                 bytes.size()); // NOLINT(readability-identifier-naming)
    return true;
}

int run(std::span<char*> args) {
    std::size_t count = 0;
    for (const char* arg : args.subspan(1)) {
        const std::filesystem::path path(arg);
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec)) {
                if (entry.is_regular_file(ec)) {
                    if (!run_file(entry.path())) {
                        return 1;
                    }
                    ++count;
                }
            }
        } else {
            if (!run_file(path)) {
                return 1;
            }
            ++count;
        }
    }
    if (count == 0) {
        std::println(stderr, "no input files");
        return 1;
    }
    std::println("replayed {} inputs", count);
    return 0;
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape): std::println in the handler may itself throw.
int main(int argc, char** argv) {
    try {
        return run(std::span(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& e) {
        std::println(stderr, "error: {}", e.what());
        return 1;
    }
}
