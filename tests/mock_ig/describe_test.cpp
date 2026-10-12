#include "mock_ig/describe.hpp"

#include <array>
#include <cigi/message/builder.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace cigi;
using namespace cigi::mock;

// Expected strings are written by hand from the formatting rules in describe.cpp: name, then
// name=value pairs, floats with 3 decimals (6 for lat/lon) and a unit suffix.

TEST(EnumName, DefinedAndUndefinedValues) {
    EXPECT_EQ(enum_name(IgMode::Operate), "Operate");
    EXPECT_EQ(enum_name(static_cast<IgMode>(7)), "7(undefined)");
    EXPECT_EQ(enum_name(EntityState::Destroyed), "Destroyed");
    EXPECT_EQ(enum_name(static_cast<EntityState>(3)), "3(undefined)");
    EXPECT_EQ(enum_name(GroundClamp::NonConformal), "NonConformal");
    EXPECT_EQ(enum_name(ComponentClass::Symbol), "Symbol");
    EXPECT_EQ(enum_name(static_cast<ComponentClass>(16)), "16(undefined)");
    EXPECT_EQ(enum_name(PixelReplicationMode::TwoByTwo), "2x2");
    EXPECT_EQ(enum_name(static_cast<PixelReplicationMode>(5)), "IgDefined5");
    EXPECT_EQ(enum_name(HatHotRequestType::Extended), "Extended");
    EXPECT_EQ(enum_name(static_cast<HatHotRequestType>(3)), "3(undefined)");
}

TEST(Describe, EveryEnumHasNamesForEveryDefinedValue) {
    // Every defined value has a real name.
    for (unsigned v = 0; v < 4; ++v) {
        EXPECT_EQ(enum_name(static_cast<IgMode>(v)).find("undefined"), std::string::npos);
    }
    for (unsigned v = 0; v < 16; ++v) {
        EXPECT_EQ(enum_name(static_cast<ComponentClass>(v)).find("undefined"), std::string::npos);
    }
    EXPECT_EQ(enum_name(EarthReferenceModel::Wgs84), "Wgs84");
    EXPECT_EQ(enum_name(EarthReferenceModel::HostDefined), "HostDefined");
    EXPECT_EQ(enum_name(AttachState::Attach), "Attach");
    EXPECT_EQ(enum_name(AnimationDirection::Backward), "Backward");
    EXPECT_EQ(enum_name(AnimationLoopMode::Continuous), "Continuous");
    EXPECT_EQ(enum_name(AnimationState::Continue), "Continue");
    EXPECT_EQ(enum_name(RateCoordinateSystem::Local), "Local");
    EXPECT_EQ(enum_name(MirrorMode::HorizontalAndVertical), "HorizontalAndVertical");
    EXPECT_EQ(enum_name(ProjectionType::OrthographicParallel), "OrthographicParallel");
    EXPECT_EQ(enum_name(HatHotCoordinateSystem::Entity), "Entity");
    EXPECT_EQ(enum_name(HatHotResponseType::Hot), "Hot");
}

TEST(Describe, IgControl) {
    IgControl p;
    p.database_number = 3;
    p.ig_mode = IgMode::Operate;
    p.timestamp_valid = true;
    p.extrapolation_enabled = true;
    p.host_frame_number = 7;
    p.timestamp = Timestamp{1000};
    p.last_ig_frame_number = 5;
    EXPECT_EQ(describe(p),
              "IgControl host_frame=7 mode=Operate db=3 extrapolation=on last_ig_frame=5 "
              "timestamp=10000us");
    IgControl q;
    q.ig_mode = static_cast<IgMode>(2);
    EXPECT_EQ(describe(q),
              "IgControl host_frame=0 mode=Debug db=0 extrapolation=off last_ig_frame=0");
}

TEST(Describe, StartOfFrame) {
    StartOfFrame p;
    p.ig_frame_number = 9;
    p.ig_mode = IgMode::Debug;
    p.database_number = -2;
    p.ig_status = 4;
    p.earth_reference_model = EarthReferenceModel::HostDefined;
    p.last_host_frame_number = 8;
    EXPECT_EQ(
        describe(p),
        "StartOfFrame ig_frame=9 mode=Debug db=-2 status=4 erm=HostDefined last_host_frame=8");
    p.timestamp_valid = true;
    p.timestamp = Timestamp{25};
    EXPECT_EQ(describe(p),
              "StartOfFrame ig_frame=9 mode=Debug db=-2 status=4 erm=HostDefined last_host_frame=8 "
              "timestamp=250us");
}

TEST(Describe, EntityControlTopLevel) {
    EntityControl p;
    p.entity_id = 5;
    p.entity_state = EntityState::Active;
    p.entity_type = 200;
    p.alpha = 255;
    p.ground_clamp = GroundClamp::Conformal;
    p.latitude_or_x = 51.5;
    p.longitude_or_y = -0.12;
    p.altitude_or_z = 1.5;
    p.roll = 1.0F;
    p.pitch = 2.0F;
    p.yaw = 90.0F;
    p.extrapolation_enabled = true;
    EXPECT_EQ(describe(p),
              "EntityControl id=5 state=Active type=200 alpha=255 attach=Detach clamp=Conformal "
              "lat=51.500000deg lon=-0.120000deg alt=1.500m roll=1.000deg pitch=2.000deg "
              "yaw=90.000deg collision=off extrapolate=on");
}

TEST(Describe, EntityControlAttachedWithAnimation) {
    EntityControl p;
    p.entity_id = 6;
    p.entity_state = EntityState::InactiveStandby;
    p.entity_type = 203;
    p.alpha = 128;
    p.attach_state = AttachState::Attach;
    p.parent_id = 5;
    p.inherit_alpha = true;
    p.latitude_or_x = -8.0;
    p.collision_detection_enabled = true;
    p.animation_state = AnimationState::Play;
    p.animation_direction = AnimationDirection::Backward;
    p.animation_loop_mode = AnimationLoopMode::Continuous;
    EXPECT_EQ(describe(p),
              "EntityControl id=6 state=InactiveStandby type=203 alpha=128 attach=Attach parent=5 "
              "inherit_alpha=on x=-8.000m y=0.000m z=0.000m roll=0.000deg pitch=0.000deg "
              "yaw=0.000deg collision=on extrapolate=off anim=Play anim_dir=Backward "
              "anim_loop=Continuous");
}

TEST(Describe, ComponentControl) {
    ComponentControl p;
    p.component_class = ComponentClass::Entity;
    p.component_id = 7;
    p.instance_id = 2;
    p.component_state = 3;
    p.data_1 = 0xDEADBEEF;
    p.data_6 = 1;
    EXPECT_EQ(describe(p), "ComponentControl class=Entity id=7 instance=2 state=3 "
                           "data=deadbeef 00000000 00000000 00000000 00000000 00000001");
}

TEST(Describe, ArticulatedPartControl) {
    ArticulatedPartControl p;
    p.entity_id = 1;
    p.articulated_part_id = 2;
    p.part_enabled = true;
    p.x_offset_enabled = true;
    p.x_offset = 1.5F;
    p.yaw_enabled = true;
    p.yaw = 45.0F;
    p.y_offset = 9.0F; // not enabled: not printed
    EXPECT_EQ(describe(p),
              "ArticulatedPartControl entity=1 part=2 enabled=on x=1.500m yaw=45.000deg");
    ArticulatedPartControl all;
    all.x_offset_enabled = all.y_offset_enabled = all.z_offset_enabled = true;
    all.roll_enabled = all.pitch_enabled = all.yaw_enabled = true;
    EXPECT_EQ(describe(all),
              "ArticulatedPartControl entity=0 part=0 enabled=off x=0.000m y=0.000m z=0.000m "
              "roll=0.000deg pitch=0.000deg yaw=0.000deg");
}

TEST(Describe, RateControl) {
    RateControl p;
    p.entity_id = 1;
    p.coordinate_system = RateCoordinateSystem::Local;
    p.x_linear_rate = 10.0F;
    p.yaw_rate = 5.0F;
    EXPECT_EQ(describe(p), "RateControl entity=1 frame=Local vx=10.000m/s vy=0.000m/s vz=0.000m/s "
                           "roll_rate=0.000deg/s pitch_rate=0.000deg/s yaw_rate=5.000deg/s");
    p.apply_to_articulated_part = true;
    p.articulated_part_id = 3;
    EXPECT_EQ(describe(p), "RateControl entity=1 part=3 vx=10.000m/s vy=0.000m/s vz=0.000m/s "
                           "roll_rate=0.000deg/s pitch_rate=0.000deg/s yaw_rate=5.000deg/s");
}

TEST(Describe, CelestialSphereControl) {
    CelestialSphereControl p;
    p.hour = 9;
    p.minute = 5;
    p.set_date(std::chrono::year_month_day{std::chrono::year{2026}, std::chrono::June,
                                           std::chrono::day{21}});
    p.date_time_valid = true;
    p.sun_enabled = true;
    p.star_field_enabled = true;
    p.star_field_intensity = 80.0F;
    EXPECT_EQ(describe(p),
              "CelestialSphereControl utc=09:05 date=2026-06-21 time_valid=on ephemeris=on sun=on "
              "moon=off stars=on star_intensity=80.0%");
    p.date = 99999999;
    EXPECT_NE(describe(p).find("date=99999999(invalid)"), std::string::npos);
}

TEST(Describe, ViewControl) {
    ViewControl p;
    p.view_id = 0;
    p.entity_id = 4;
    p.x_offset_enabled = true;
    p.x_offset = 1.0F;
    p.z_offset_enabled = true;
    p.z_offset = -1.0F;
    EXPECT_EQ(describe(p), "ViewControl view=0 entity=4 x=1.000m z=-1.000m");
    p.group_id = 7;
    p.roll_enabled = p.pitch_enabled = true;
    p.pitch = 10.0F;
    EXPECT_EQ(describe(p),
              "ViewControl group=7 entity=4 x=1.000m z=-1.000m roll=0.000deg pitch=10.000deg");
}

TEST(Describe, ViewDefinition) {
    ViewDefinition p;
    p.view_id = 1;
    p.near_enabled = p.far_enabled = p.left_enabled = true;
    p.right_enabled = p.top_enabled = p.bottom_enabled = true;
    p.near_plane = 1.0F;
    p.far_plane = 20000.0F;
    p.left = -35.0F;
    p.right = 35.0F;
    p.top = 21.0F;
    p.bottom = -21.0F;
    EXPECT_EQ(describe(p),
              "ViewDefinition view=1 group=0 projection=Perspective mirror=None replication=None "
              "type=0 near=1.000m far=20000.000m left=-35.000deg right=35.000deg top=21.000deg "
              "bottom=-21.000deg");
    ViewDefinition q;
    q.view_id = 2;
    q.group_id = 3;
    q.projection_type = ProjectionType::OrthographicParallel;
    q.mirror_mode = MirrorMode::Vertical;
    q.pixel_replication_mode = PixelReplicationMode::OneByTwo;
    q.view_type = 4;
    EXPECT_EQ(describe(q),
              "ViewDefinition view=2 group=3 projection=OrthographicParallel mirror=Vertical "
              "replication=1x2 type=4");
}

TEST(Describe, HatHotRequest) {
    HatHotRequest g;
    g.hat_hot_id = 3;
    g.request_type = HatHotRequestType::Extended;
    g.update_period = 30;
    g.latitude_or_x = 51.5;
    g.longitude_or_y = -0.12;
    g.altitude_or_z = 500.0;
    EXPECT_EQ(describe(g),
              "HatHotRequest id=3 type=Extended period=30 frame=Geodetic lat=51.500000deg "
              "lon=-0.120000deg alt=500.000m");
    HatHotRequest e;
    e.hat_hot_id = 4;
    e.coordinate_system = HatHotCoordinateSystem::Entity;
    e.entity_id = 1;
    e.altitude_or_z = -2.0;
    EXPECT_EQ(describe(e),
              "HatHotRequest id=4 type=Hat period=one-shot frame=Entity entity=1 x=0.000m "
              "y=0.000m z=-2.000m");
}

TEST(Describe, HatHotResponses) {
    HatHotResponse r;
    r.hat_hot_id = 1;
    r.valid = true;
    r.height = 380.0;
    EXPECT_EQ(describe(r),
              "HatHotResponse id=1 valid=on type=Hat height=380.000m host_frame_lsn=0");
    r.valid = false;
    r.response_type = HatHotResponseType::Hot;
    r.host_frame_lsn = 5;
    EXPECT_EQ(describe(r), "HatHotResponse id=1 valid=off type=Hot host_frame_lsn=5");

    HatHotExtendedResponse x;
    x.hat_hot_id = 3;
    x.valid = true;
    x.hat = 380.0;
    x.hot = 120.0;
    x.material_code = 1;
    x.normal_azimuth = 121.968537F;
    x.normal_elevation = 83.911576F;
    x.host_frame_lsn = 2;
    EXPECT_EQ(describe(x),
              "HatHotExtendedResponse id=3 valid=on hat=380.000m hot=120.000m material=1 "
              "normal_az=121.969deg normal_el=83.912deg host_frame_lsn=2");
    x.valid = false;
    EXPECT_EQ(describe(x), "HatHotExtendedResponse id=3 valid=off host_frame_lsn=2");
}

TEST(Describe, UnknownPacket) {
    const std::array<std::byte, 8> bytes{};
    UnknownPacket u;
    u.opcode = 201;
    u.bytes = bytes;
    EXPECT_EQ(describe(u), "UnknownPacket opcode=201 size=8");
}

TEST(Describe, AnyPacketVariants) {
    const AnyPacket<HostToIgPackets> host{std::in_place_type<RateControl>,
                                          RateControl{.entity_id = 2}};
    EXPECT_EQ(describe_any(host), describe(RateControl{.entity_id = 2}));
    const AnyPacket<IgToHostPackets> ig{std::in_place_type<HatHotResponse>,
                                        HatHotResponse{.hat_hot_id = 9}};
    EXPECT_EQ(describe_any(ig), describe(HatHotResponse{.hat_hot_id = 9}));
    UnknownPacket raw;
    raw.opcode = 250;
    const AnyPacket<HostToIgPackets> unknown{std::in_place_type<UnknownPacket>, raw};
    EXPECT_EQ(describe_any(unknown), "UnknownPacket opcode=250 size=0");
}

TEST(Describe, FromADecodedMessage) {
    StaticMessageBuilder<256> msg(Direction::HostToIg);
    ASSERT_TRUE(msg.add(IgControl{.host_frame_number = 12}));
    ASSERT_TRUE(msg.add(RateControl{.entity_id = 2, .x_linear_rate = 1.0F}));
    const auto view = MessageView::parse(msg.bytes());
    ASSERT_TRUE(view);
    std::vector<std::string> lines;
    for (const auto& item : *view) {
        ASSERT_TRUE(item);
        const auto decoded = decode_any<HostToIgPackets>(*item, view->direction());
        ASSERT_TRUE(decoded);
        lines.push_back(describe_any(*decoded));
    }
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0],
              "IgControl host_frame=12 mode=ResetStandby db=0 extrapolation=off last_ig_frame=0");
    EXPECT_EQ(lines[1].rfind("RateControl entity=2 frame=WorldParent vx=1.000m/s", 0), 0U);
}

} // namespace
