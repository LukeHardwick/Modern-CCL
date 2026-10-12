#pragma once

// Messages for the demo tests.

#include <cigi/message/builder.hpp>
#include <cigi/packets.hpp>
#include <cstddef>
#include <gtest/gtest.h>
#include <vector>

namespace cigi::demo::test {

using namespace cigi;

inline std::vector<std::byte> host_message() {
    std::vector<std::byte> storage(256);
    MessageBuilder builder(Direction::HostToIg, storage);
    IgControl control;
    control.ig_mode = IgMode::Operate;
    EXPECT_TRUE(builder.add(control));
    EntityControl entity;
    entity.entity_id = 7;
    entity.entity_state = EntityState::Active;
    entity.entity_type = 101;
    EXPECT_TRUE(builder.add(entity));
    const auto bytes = builder.bytes();
    return {bytes.begin(), bytes.end()};
}

inline std::vector<std::byte> ig_message() {
    std::vector<std::byte> storage(64);
    MessageBuilder builder(Direction::IgToHost, storage);
    EXPECT_TRUE(builder.add(StartOfFrame{.ig_mode = IgMode::Operate}));
    const auto bytes = builder.bytes();
    return {bytes.begin(), bytes.end()};
}

} // namespace cigi::demo::test
