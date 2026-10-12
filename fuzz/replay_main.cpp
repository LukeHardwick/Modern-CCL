// Corpus-replay driver: runs LLVMFuzzerTestOneInput over every file (directories are walked) named
// on the command line. Used by the ctests in normal builds, where libFuzzer is not linked.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <system_error>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

bool run_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", path.string().c_str());
        return false;
    }
    const std::vector<char> raw{std::istreambuf_iterator<char>(in), {}};
    std::vector<std::uint8_t> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(raw[i]);
    }
    (void)LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::size_t count = 0;
    for (const char* arg : std::span(argv, static_cast<std::size_t>(argc)).subspan(1)) {
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
        std::fputs("no input files\n", stderr);
        return 1;
    }
    std::printf("replayed %zu inputs\n", count);
    return 0;
}
