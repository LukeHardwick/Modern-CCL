// cigi_db: validate and list mock-IG databases. See cli.hpp for the commands.

#include "cigi_db/cli.hpp"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    return cigi::db::run(args, std::cout, std::cerr);
}
