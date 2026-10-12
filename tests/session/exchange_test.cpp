// Scripted exchanges between a HostSession and an IgSession wired back to back in memory.

#include "support.hpp"

#include <gtest/gtest.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>

namespace {

using namespace cigi;
using namespace cigi::test;

/// Host and IG joined by two "wires". `ig_frame()` sends one Start of Frame to the Host and
/// `host_frame()` answers it, the way the synchronous loop of ICD 2.2.1 runs.
class Rig {
public:
    Rig() : Rig(plain(), plain()) {}
    explicit Rig(SessionConfig host_config) : Rig(std::move(host_config), plain()) {}
    Rig(SessionConfig host_config, SessionConfig ig_config)
        : host_(with_clock(std::move(host_config))), ig_(with_clock(std::move(ig_config))) {}

    HostSession& host() { return host_; }
    IgSession& ig() { return ig_; }

    /// IG -> Host. Returns the datagram (so a test can drop or alter it).
    Bytes ig_message() {
        EXPECT_TRUE(ig_.begin_frame());
        return to_vec(ig_.finish_frame());
    }
    void ig_frame() { EXPECT_TRUE(host_.receive(ig_message())); }

    /// Host -> IG; `fill` adds packets once the Host is operational.
    template <class Fill> Bytes host_message(Fill&& fill) {
        EXPECT_TRUE(host_.begin_frame());
        std::forward<Fill>(fill)(host_);
        return to_vec(host_.finish_frame());
    }
    Bytes host_message() {
        return host_message([](HostSession&) {});
    }
    void host_frame() {
        if (host_.ready_to_send()) {
            EXPECT_TRUE(ig_.receive(host_message()));
        }
    }
    /// One full frame pair.
    void round() {
        ig_frame();
        host_frame();
    }

private:
    static SessionConfig plain() { return {}; }
    [[nodiscard]] SessionConfig with_clock(SessionConfig c) const {
        if (!c.clock) {
            c.clock = clock_.fn();
        }
        return c;
    }
    FakeClock clock_;
    HostSession host_;
    IgSession ig_;
};

TEST(Exchange, StartupFromStandbyToOperate) {
    Rig rig;
    rig.host().request_mode(IgMode::Operate).value();
    // The IG accepts the request but takes two frames to get ready.
    int asked = 0;
    rig.ig().on_mode_request([&](IgMode) {
        ++asked;
        return true;
    });

    rig.round(); // IG in Standby; Host answers with IG Control(Operate)
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(rig.host().ig_mode(), IgMode::ResetStandby);
    EXPECT_FALSE(rig.host().operational());
    EXPECT_EQ(rig.host().begin_frame().has_value(), true);
    EXPECT_EQ(rig.host().add(EntityControl{}).error().code, ErrorCode::NotOperational);
    static_cast<void>(rig.host().finish_frame());

    rig.round(); // still preparing
    EXPECT_EQ(asked, 1);

    rig.ig().set_mode(IgMode::Operate);
    rig.ig_frame();
    EXPECT_TRUE(rig.host().operational());
    EXPECT_EQ(rig.host().ig_mode(), IgMode::Operate);

    // Now mission data flows and the IG delivers it.
    std::vector<std::uint16_t> entities;
    rig.ig().handlers().on<EntityControl>(
        [&](const EntityControl& e) { entities.push_back(e.entity_id); });
    const Bytes m = rig.host_message([](HostSession& h) {
        EXPECT_TRUE(h.add(EntityControl{.entity_id = 12}));
        EXPECT_TRUE(h.add(EntityControl{.entity_id = 13}));
    });
    ASSERT_TRUE(rig.ig().receive(m));
    EXPECT_EQ(entities, (std::vector<std::uint16_t>{12, 13}));
}

TEST(Exchange, ModeRefusalLeavesTheHostInStandby) {
    Rig rig;
    rig.host().request_mode(IgMode::Operate).value();
    rig.ig().on_mode_request([](IgMode) { return false; });
    for (int i = 0; i < 5; ++i) {
        rig.round();
    }
    EXPECT_EQ(rig.host().ig_mode(), IgMode::ResetStandby);
    EXPECT_FALSE(rig.host().operational());
    EXPECT_EQ(rig.ig().stats().mode_requests_refused, 1U);
    EXPECT_EQ(rig.host().stats().lost_frames, 0U);
}

TEST(Exchange, FrameNumbersFollowFigureSix) {
    // ICD 2.3 Figure 6: each Start of Frame acknowledges the Host's last frame, each IG Control
    // acknowledges the IG's last frame.
    SessionConfig host_config;
    host_config.initial_frame_number = 100;
    SessionConfig ig_config;
    ig_config.initial_frame_number = 50;
    Rig rig{host_config, ig_config};

    const Bytes sof1 = rig.ig_message();
    EXPECT_EQ(first_as<StartOfFrame>(sof1).ig_frame_number, 50U);
    ASSERT_TRUE(rig.host().receive(sof1));
    const Bytes control1 = rig.host_message();
    EXPECT_EQ(first_as<IgControl>(control1).host_frame_number, 100U);
    EXPECT_EQ(first_as<IgControl>(control1).last_ig_frame_number, 50U);
    ASSERT_TRUE(rig.ig().receive(control1));

    const Bytes sof2 = rig.ig_message();
    EXPECT_EQ(first_as<StartOfFrame>(sof2).ig_frame_number, 51U);
    EXPECT_EQ(first_as<StartOfFrame>(sof2).last_host_frame_number, 100U);
    ASSERT_TRUE(rig.host().receive(sof2));
    const Bytes control2 = rig.host_message();
    EXPECT_EQ(first_as<IgControl>(control2).host_frame_number, 101U);
    EXPECT_EQ(first_as<IgControl>(control2).last_ig_frame_number, 51U);
    ASSERT_TRUE(rig.ig().receive(control2));
    EXPECT_EQ(rig.ig().host_frame_lsn(), 101U & 0xFU);
}

TEST(Exchange, FrameNumbersWrapOnBothSides) {
    SessionConfig host_config;
    host_config.initial_frame_number = 0xFFFFFFFEU;
    SessionConfig ig_config;
    ig_config.initial_frame_number = 0xFFFFFFFFU;
    Rig rig{host_config, ig_config};
    for (int i = 0; i < 5; ++i) {
        rig.round();
    }
    EXPECT_EQ(rig.host().stats().lost_frames, 0U);
    EXPECT_EQ(rig.ig().stats().lost_frames, 0U);
    EXPECT_EQ(rig.host().stats().frame_regressions, 0U);
    EXPECT_EQ(rig.ig().stats().frame_regressions, 0U);
    EXPECT_EQ(rig.host().last_ig_frame_number(), 3U);
    EXPECT_EQ(rig.ig().last_host_frame_number(), 2U);
}

TEST(Exchange, LostMessagesAreCountedOnBothSides) {
    Rig rig;
    rig.round();
    // The IG's next two Start of Frame messages never arrive; the third does.
    static_cast<void>(rig.ig_message());
    static_cast<void>(rig.ig_message());
    rig.round();
    EXPECT_EQ(rig.host().stats().lost_frames, 2U);

    // The Host's next message is lost too.
    ASSERT_TRUE(rig.host().receive(rig.ig_message()));
    static_cast<void>(rig.host_message());
    rig.round();
    EXPECT_EQ(rig.ig().stats().lost_frames, 1U);
}

TEST(Exchange, SynchronousHostWaitsForTheIg) {
    Rig rig;
    EXPECT_FALSE(rig.host().ready_to_send());
    rig.ig_frame();
    EXPECT_TRUE(rig.host().ready_to_send());
    rig.host_frame();
    EXPECT_FALSE(rig.host().ready_to_send());
}

TEST(Exchange, AsynchronousHostSendsAtItsOwnPace) {
    SessionConfig host_config;
    host_config.synchronous = false;
    Rig rig{host_config};
    ASSERT_TRUE(rig.ig().receive(rig.host_message()));
    ASSERT_TRUE(rig.ig().receive(rig.host_message()));
    ASSERT_TRUE(rig.ig().receive(rig.host_message()));
    EXPECT_EQ(rig.ig().stats().lost_frames, 0U);
    EXPECT_EQ(rig.host().stats().messages_received, 0U);
    EXPECT_TRUE(rig.host().ready_to_send());
    EXPECT_TRUE(first_as<IgControl>(rig.host_message()).timestamp_valid); // required when async
}

TEST(Exchange, DatabaseLoadHandshakeFollowsFigureThirtyEight) {
    Rig rig;
    rig.host().request_mode(IgMode::Operate).value();
    rig.round(); // the request is accepted at once (no callback) and the IG is operating
    rig.ig_frame();
    ASSERT_TRUE(rig.host().operational());

    std::vector<int> requests;
    rig.ig().on_database_request([&](std::int8_t n) { requests.push_back(n); });
    rig.ig().set_database(2); // database m is loaded
    rig.ig_frame();
    EXPECT_EQ(rig.host().ig_database(), 2);

    rig.host().request_database(7).value();
    rig.host_frame(); // IG Control: database +7
    EXPECT_EQ(requests, (std::vector<int>{7}));

    rig.ig().set_database(-7); // the IG begins loading
    rig.ig_frame();
    EXPECT_TRUE(rig.host().database_loading());
    EXPECT_EQ(rig.host().pending_database(), 0);
    const Bytes ack = rig.host_message();
    EXPECT_EQ(first_as<IgControl>(ack).database_number, 0); // the Host acknowledges
    ASSERT_TRUE(rig.ig().receive(ack));
    EXPECT_EQ(requests.size(), 1U);

    rig.ig_frame(); // still loading
    EXPECT_TRUE(rig.host().database_loading());
    rig.ig().set_database(7);
    rig.ig_frame();
    EXPECT_FALSE(rig.host().database_loading());
    EXPECT_EQ(rig.host().ig_database(), 7);
    EXPECT_TRUE(rig.host().operational());
}

TEST(Exchange, HatHotRequestIsAnsweredWithTheHostFrameLsn) {
    Rig rig;
    rig.host().request_mode(IgMode::Operate).value();
    rig.round();
    rig.ig_frame();

    rig.ig().handlers().on<HatHotRequest>([&](const HatHotRequest& request) {
        EXPECT_TRUE(rig.ig().begin_frame());
        HatHotResponse response;
        response.hat_hot_id = request.hat_hot_id;
        response.valid = true;
        response.host_frame_lsn = rig.ig().host_frame_lsn();
        response.height = 12.5;
        EXPECT_TRUE(rig.ig().add(response));
    });
    HatHotRequest request;
    request.hat_hot_id = 77;
    const Bytes m = rig.host_message([&](HostSession& h) { EXPECT_TRUE(h.add(request)); });
    ASSERT_TRUE(rig.ig().receive(m));
    const std::uint8_t frame_lsn = host_frame_lsn(first_as<IgControl>(m).host_frame_number);

    // The IG started its response message from the handler; the app finishes it.
    const Bytes reply = to_vec(rig.ig().finish_frame());
    std::optional<HatHotResponse> got;
    rig.host().handlers().on<HatHotResponse>([&](const HatHotResponse& r) { got = r; });
    ASSERT_TRUE(rig.host().receive(reply));
    ASSERT_TRUE(got);
    EXPECT_EQ(got->hat_hot_id, 77);
    EXPECT_EQ(got->host_frame_lsn, frame_lsn);
    EXPECT_DOUBLE_EQ(got->height, 12.5);
}

TEST(Exchange, ForeignByteOrderPeersInteroperate) {
    Rig rig;
    rig.host().request_mode(IgMode::Operate).value();

    // Everything the IG sends is rewritten as a foreign-endian peer would have sent it.
    Bytes sof = rig.ig_message();
    swap_frame_fields(sof);
    ASSERT_TRUE(rig.host().receive(sof));
    EXPECT_EQ(unwrap(rig.host().peer()).byte_order, wire::ByteOrder::Swapped);
    Bytes control = rig.host_message();
    swap_frame_fields(control);
    ASSERT_TRUE(rig.ig().receive(control));
    EXPECT_EQ(unwrap(rig.ig().peer()).byte_order, wire::ByteOrder::Swapped);
    EXPECT_EQ(rig.ig().mode(), IgMode::Operate);
    EXPECT_EQ(rig.ig().last_host_frame_number(), 0U);
    EXPECT_EQ(rig.host().stats().decode_errors, 0U);
}

TEST(Exchange, LegacyIgThreeOne) {
    // A 3.1 IG sends 16-byte Start of Frame packets carrying a Frame Counter; the Host echoes the
    // counter back as its own frame number (ICD 2.3 note) and masks 3.2+/3.3 content.
    Rig rig;
    rig.host().request_mode(IgMode::Operate).value();
    for (std::uint32_t counter : {500U, 501U, 502U}) {
        Bytes sof = frame_packet(101, 16, 1);
        sof.at(5) = static_cast<std::byte>((1U << 4U) | 1U);
        wire::store<std::uint32_t>(sof, 8, counter);
        ASSERT_TRUE(rig.host().receive(sof));
        Bytes m = rig.host_message([](HostSession& h) {
            HatHotRequest request;
            request.update_period = 3;
            EXPECT_TRUE(h.add(request));
        });
        EXPECT_EQ(first_as<IgControl>(m).host_frame_number, counter);
        EXPECT_EQ(unwrap(find_packet<HatHotRequest>(m)).update_period, 0);
    }
    EXPECT_EQ(rig.host().stats().lost_frames, 0U);
    EXPECT_EQ(unwrap(rig.host().peer()).version, version_of(3, 1));
}

TEST(Exchange, LegacyHostThreeOne) {
    Rig rig;
    rig.ig().set_mode(IgMode::Operate);
    std::vector<int> periods;
    rig.ig().handlers().on<HatHotRequest>(
        [&](const HatHotRequest& r) { periods.push_back(r.update_period); });
    for (std::uint32_t counter : {7U, 8U}) {
        Bytes m = frame_packet(1, 16, 1);
        m.at(4) = static_cast<std::byte>((1U << 4U) | 1U);
        wire::store<std::uint32_t>(m, 8, counter);
        HatHotRequest request;
        request.update_period = 5; // reserved in 3.1: a conforming peer writes 0; this one did not
        append(m, encoded(request));
        ASSERT_TRUE(rig.ig().receive(m));
    }
    EXPECT_EQ(periods, (std::vector<int>{0, 0}));
    EXPECT_EQ(rig.ig().last_host_frame_number(), 8U);
    EXPECT_EQ(rig.ig().stats().lost_frames, 0U);
}

TEST(Exchange, TimestampsWrapWithoutBreakingTheSessions) {
    const FakeClock clock{0xFFFFFFF0U};
    SessionConfig host_config;
    host_config.clock = clock.fn();
    SessionConfig ig_config;
    ig_config.clock = clock.fn();
    Rig rig{host_config, ig_config};
    std::vector<std::uint32_t> stamps;
    for (int i = 0; i < 4; ++i) {
        clock.advance(10);
        stamps.push_back(first_as<IgControl>(rig.host_message()).timestamp.count());
    }
    EXPECT_EQ(stamps, (std::vector<std::uint32_t>{0xFFFFFFFAU, 4U, 14U, 24U}));
}

TEST(Exchange, SessionsLogToAnInjectedLogger) {
    std::ostringstream out;
    const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
    sink->set_pattern("%l: %v");
    const auto logger = std::make_shared<spdlog::logger>("test", sink);
    logger->set_level(spdlog::level::debug);

    SessionConfig host_config;
    host_config.logger = logger;
    SessionConfig ig_config;
    ig_config.logger = logger;
    Rig rig{host_config, ig_config};
    rig.host().request_mode(IgMode::Operate).value();
    rig.ig_frame();
    ASSERT_TRUE(rig.host().begin_frame());
    EXPECT_FALSE(rig.host().add(HatHotRequest{})); // not operational yet: logged at debug
    static_cast<void>(rig.host().finish_frame());
    rig.host_frame();
    static_cast<void>(rig.ig_message()); // lost
    rig.round();
    rig.ig_frame();

    const std::string log = out.str();
    EXPECT_NE(log.find("IG speaks CIGI 3.3"), std::string::npos) << log;
    EXPECT_NE(log.find("Host speaks CIGI 3.3"), std::string::npos) << log;
    EXPECT_NE(log.find("IG mode is Operate"), std::string::npos) << log;
    EXPECT_NE(log.find("Host requests mode Operate"), std::string::npos) << log;
    EXPECT_NE(log.find("lost 1 IG message(s)"), std::string::npos) << log;
    EXPECT_NE(log.find("NotOperational"), std::string::npos) << log;
}

TEST(Exchange, TheDefaultLoggerIsSilent) {
    const auto logger = detail::resolve_logger(SessionConfig{});
    ASSERT_TRUE(logger);
    EXPECT_FALSE(logger->should_log(spdlog::level::critical));
}

} // namespace
