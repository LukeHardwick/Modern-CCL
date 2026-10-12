// The Ig facade with a SceneModel registered still reports errors and unknown opcodes (the scene
// used to overwrite the session's hooks).
#include "mock_ig/scene.hpp"

#include <array>
#include <cigi/datagram_transport.hpp>
#include <cigi/ig.hpp>
#include <cigi/message/builder.hpp>
#include <gtest/gtest.h>
#include <vector>

namespace cigi::demo {
namespace {

using namespace std::chrono_literals;

TEST(IgWithScene, FacadeErrorAndUnknownHooksStillFire) {
    auto [ig_side, host_side] = make_memory_transport_pair();
    const IgConfig config;
    auto ig = Ig::over(config, std::move(ig_side));
    ASSERT_TRUE(ig.has_value());

    mock::SceneModel scene;
    scene.register_handlers(ig->session().handlers());
    int errors = 0;
    int unknowns = 0;
    ig->on_error([&](const Error& e) {
        ++errors;
        scene.note_error(e);
    });
    ig->on_unknown([&](const UnknownPacket& p) {
        ++unknowns;
        scene.note_unknown(p);
    });
    ig->set_mode(IgMode::Operate); // packets after IG Control only count in Operate

    std::vector<std::byte> storage(256);
    MessageBuilder builder(Direction::HostToIg, storage);
    IgControl control;
    control.ig_mode = IgMode::Operate;
    ASSERT_TRUE(builder.add(control));
    std::vector<std::byte> bytes(builder.bytes().begin(), builder.bytes().end());
    // An Entity Control (opcode 2) that claims to be 8 bytes long: a body decode error; then an
    // unknown opcode (8 bytes).
    for (const std::uint8_t b :
         std::array<std::uint8_t, 16>{2, 8, 0, 0, 0, 0, 0, 0, 222, 8, 0, 0, 0, 0, 0, 0}) {
        bytes.push_back(static_cast<std::byte>(b));
    }
    ASSERT_TRUE(host_side->send(bytes));
    ASSERT_TRUE(ig->poll(50ms));

    EXPECT_EQ(errors, 1);
    EXPECT_EQ(unknowns, 1);
    EXPECT_EQ(scene.stats().decode_errors, 1U);
    EXPECT_EQ(scene.stats().unknown_packets, 1U);
}

} // namespace
} // namespace cigi::demo
