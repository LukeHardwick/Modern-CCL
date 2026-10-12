// demo_ig: a mock CIGI 3.3 Image Generator without graphics. It follows the start-up sequence,
// keeps a model of everything the Host commands, answers HAT/HOT requests and can misbehave on
// demand (--refuse-operate, --drop, ...). See docs/guide/trying-it-out.md.
#include "demo_common/demo_ig.hpp"
#include "demo_common/support.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/logger.h>

// CLI11 and spdlog may throw (an app may terminate); CLI11_PARSE takes char**.
// NOLINTNEXTLINE(bugprone-exception-escape,misc-const-correctness)
int main(int argc, char** argv) {
    cigi::demo::DemoIgOptions options;
    CLI::App app{"Mock CIGI 3.3 Image Generator (headless)", "demo_ig"};
    cigi::demo::add_demo_ig_options(app, options);
    CLI11_PARSE(app, argc, argv);

    const auto logger =cigi::demo::make_logger("ig", options.log_level);
    auto ig = cigi::demo::DemoIg::create(options, logger);
    if (!ig) {
        logger->error("{}", ig.error());
        return 2;
    }
    const cigi::demo::RunControl control;
    return (*ig)->run(control.token()).exit_code();
}
