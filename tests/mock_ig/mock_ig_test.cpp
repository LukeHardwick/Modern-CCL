#include "mock_ig/mock_ig.hpp"

#include <array>
#include <cigi/message/builder.hpp>
#include <gtest/gtest.h>
#include <vector>

namespace {

using namespace cigi;
using namespace cigi::mock;

/// Host message with an IgControl in `mode`, run through one IG frame; returns the Start of
/// Frame mode of the reply.
IgMode reply_mode(IgMode mode) {
    MockIg ig;
    std::array<std::byte, 256> storage{};
    MessageBuilder host(Direction::HostToIg, storage);
    EXPECT_TRUE(host.add(IgControl{.ig_mode = mode}));
    EXPECT_TRUE(ig.ingest(host.bytes()));
    const auto replies = ig.end_frame(1.0 / 60.0);
    std::array<std::byte, 256> out{};
    MessageBuilder builder(Direction::IgToHost, out);
    EXPECT_TRUE(ig.build_reply(builder, replies));
    const auto view = MessageView::parse(builder.bytes());
    EXPECT_TRUE(view);
    for (const auto& item : *view) {
        if (item && item->opcode == StartOfFrame::opcode) {
            const auto sof = item->decode_as<StartOfFrame>();
            EXPECT_TRUE(sof);
            return sof->ig_mode;
        }
    }
    ADD_FAILURE() << "no Start of Frame";
    return IgMode::ResetStandby;
}

TEST(MockIg, StartOfFrameEchoesOperateAndDebug) {
    EXPECT_EQ(reply_mode(IgMode::Operate), IgMode::Operate);
    EXPECT_EQ(reply_mode(IgMode::Debug), IgMode::Debug);
}

TEST(MockIg, StartOfFrameReportsResetStandbyForEveryOtherMode) {
    EXPECT_EQ(reply_mode(IgMode::ResetStandby), IgMode::ResetStandby);
    EXPECT_EQ(reply_mode(IgMode::OfflineMaintenance), IgMode::ResetStandby);
}

TEST(MockIg, IngestRejectsADatagramThatIsNotCigi) {
    MockIg ig;
    const std::array<std::byte, 3> junk{std::byte{1}, std::byte{2}, std::byte{3}};
    const auto stats = ig.ingest(junk);
    EXPECT_FALSE(stats);
    EXPECT_EQ(ig.ig_frame(), 0U);
}

TEST(MockIg, BuildReplyReportsABufferThatIsTooSmall) {
    MockIg ig;
    const auto replies = ig.end_frame(0.1);
    std::array<std::byte, 4> tiny{};
    MessageBuilder builder(Direction::IgToHost, tiny);
    const auto r = ig.build_reply(builder, replies);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::BufferTooSmall);
}

TEST(MockIg, BuildReplyReportsAnAnswerThatDoesNotFit) {
    const MockIg ig;
    const MockIg::Replies replies{HatHotResponse{}, HatHotResponse{}};
    // Room for Start of Frame only.
    std::array<std::byte, 24> small{};
    MessageBuilder probe(Direction::IgToHost, small);
    ASSERT_TRUE(probe.add(StartOfFrame{}));
    std::array<std::byte, 24> storage{};
    MessageBuilder builder(Direction::IgToHost, std::span(storage).first(probe.size()));
    const auto r = ig.build_reply(builder, replies);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::BufferTooSmall);
}

TEST(MockIg, AccessorsExposeTheSceneAndTheResponder) {
    MockIg ig;
    const MockIg& cig = ig;
    EXPECT_EQ(&ig.scene(), &cig.scene());
    EXPECT_EQ(&ig.hat_hot(), &cig.hat_hot());
    EXPECT_TRUE(cig.scene().entities().empty());
}

} // namespace
