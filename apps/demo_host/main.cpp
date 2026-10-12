// demo_host: a CIGI 3.3 Host that needs no real IG. It waits for Start of Frame, requests Operate,
// then flies an ownship in a circle with a view, sky time, an animated part and HAT/HOT requests.
// Exit code 0 only if responses arrived and no protocol error occurred. See
// docs/guide/trying-it-out.md.
#include "demo_common/demo_host.hpp"
#include "demo_common/support.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/logger.h>

// CLI11 and spdlog may throw (an app may terminate); CLI11_PARSE takes char**.
// NOLINTNEXTLINE(bugprone-exception-escape,misc-const-correctness)
int main(int argc, char** argv) {
    cigi::demo::DemoHostOptions options;
    CLI::App app{"CIGI 3.3 demo Host", "demo_host"};
    cigi::demo::add_demo_host_options(app, options);
    CLI11_PARSE(app, argc, argv);

    const auto logger =cigi::demo::make_logger("host", options.log_level);
    auto host = cigi::demo::DemoHost::create(options, logger);
    if (!host) {
        logger->error("{}", host.error());
        return 2;
    }
    const cigi::demo::RunControl control;
    return (*host)->run(control.token()).exit_code();
}
