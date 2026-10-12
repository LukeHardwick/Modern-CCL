#include "mock_ig/scripted_host.hpp"

#include <algorithm>
#include <cigi/packets.hpp>
#include <cmath>
#include <numbers>
#include <optional>
#include <random>

namespace cigi::mock {

namespace {

constexpr double gravity_mps2 = 9.80665;
constexpr double tau = 2.0 * std::numbers::pi;

// Convoy loop (ENU metres from the origin).
constexpr Vec3d convoy_centre{700.0, -300.0, 0.0};
constexpr double convoy_radius_m = 350.0;
constexpr double convoy_speed_mps = 10.0;
constexpr double convoy_spacing_rad = 0.12;

constexpr std::uint16_t ownship_id = 0;
constexpr std::uint16_t trailer_id = 4;
constexpr std::uint16_t cargo_id = 5;

constexpr double cargo_rate_north_mps = 8.0;
constexpr double cargo_rate_east_mps = 4.0;
constexpr double cargo_agl_m = 30.0;
constexpr float turret_yaw_rate_dps = 36.0F;
constexpr float propeller_roll_rate_dps = 720.0F;

/// Position on a clockwise circle: phase 0 is due north of the centre, heading east.
Vec3d on_circle(const Vec3d& centre, double radius, double phase_rad) noexcept {
    return {centre.x + (radius * std::sin(phase_rad)), centre.y + (radius * std::cos(phase_rad)),
            0.0};
}

double circle_heading_deg(double phase_rad) noexcept {
    // Velocity of a clockwise circle is (cos, -sin) in (east, north): heading = phase + 90.
    return wrap_360((phase_rad * rad_to_deg) + 90.0);
}

double vehicle_phase(int index, double t_s) noexcept {
    return (-convoy_spacing_rad * (index - 1)) + (convoy_speed_mps / convoy_radius_m * t_s);
}

} // namespace

ScriptedHost::ScriptedHost(const ScriptedHostConfig& config)
    : config_(config), frame_of_(config.origin) {
    for (std::uint16_t i = 0; i < config_.extra_entities; ++i) {
        // Fixed seeds and raw engine output (no std distributions) keep this identical on every
        // platform.
        std::mt19937 rng(1000U + i);
        const auto pick = [&rng](std::uint32_t lo, std::uint32_t span) {
            return static_cast<std::uint32_t>(lo + (rng() % span));
        };
        Mover m;
        m.id = static_cast<std::uint16_t>(first_extra_id + i);
        switch (i % 3) {
        case 0: // aircraft orbiting the origin
            m.entity_type = 110;
            m.aircraft = true;
            m.radius_m = pick(500, 2500);
            m.speed_mps = pick(40, 60);
            m.agl_m = pick(50, 350);
            break;
        case 1: // ground vehicle on a small loop elsewhere
            m.entity_type = 210;
            m.centre = {static_cast<double>(pick(0, 4000)) - 2000.0,
                        static_cast<double>(pick(0, 4000)) - 2000.0, 0.0};
            m.radius_m = pick(50, 200);
            m.speed_mps = pick(5, 10);
            break;
        default: // something else, hovering low
            m.entity_type = 50;
            m.aircraft = true;
            m.centre = {static_cast<double>(pick(0, 4000)) - 2000.0,
                        static_cast<double>(pick(0, 4000)) - 2000.0, 0.0};
            m.radius_m = pick(20, 100);
            m.speed_mps = pick(2, 8);
            m.agl_m = 20.0;
            break;
        }
        m.phase_rad = static_cast<double>(pick(0, 6283)) / 1000.0;
        extras_.push_back(m);
    }
    storage_.resize(4096 + (static_cast<std::size_t>(config_.extra_entities) * 48));
}

Vec3d ScriptedHost::ownship_enu(double t_s) const noexcept {
    const double phase = config_.ownship_speed_mps / config_.circle_radius_m * t_s;
    return on_circle({}, config_.circle_radius_m, phase);
}

double ScriptedHost::ownship_heading_deg(double t_s) const noexcept {
    return circle_heading_deg(config_.ownship_speed_mps / config_.circle_radius_m * t_s);
}

Vec3d ScriptedHost::vehicle_enu(int index, double t_s) noexcept {
    return on_circle(convoy_centre, convoy_radius_m, vehicle_phase(index, t_s));
}

double ScriptedHost::vehicle_heading_deg(int index, double t_s) noexcept {
    return circle_heading_deg(vehicle_phase(index, t_s));
}

std::uint32_t ScriptedHost::detach_frame() const noexcept {
    return static_cast<std::uint32_t>(std::lround(config_.detach_s / config_.dt_s));
}

Result<void> ScriptedHost::build_frame(MessageBuilder& builder) {
    const double t = frame_ * config_.dt_s;
    const bool refresh = frame_ % 60 == 0; // slow-changing packets are repeated once a second

    // The first builder error stops further adds and is returned at the end.
    std::optional<Error> failure;
    const auto add = [&](const auto& packet) {
        if (failure) {
            return;
        }
        if (auto r = builder.add(packet); !r) {
            failure = r.error();
        }
    };

    add(IgControl{
        .ig_mode = IgMode::Operate,
        .host_frame_number = frame_,
    });

    // ---- sky ----
    {
        const double total_min = std::floor((config_.start_utc_hours * 60.0) + t);
        const auto day_offset = static_cast<int>(std::floor(total_min / 1440.0));
        const auto minute_of_day = static_cast<int>(total_min - (day_offset * 1440.0));
        CelestialSphereControl sky;
        sky.hour = static_cast<std::uint8_t>(minute_of_day / 60);
        sky.minute = static_cast<std::uint8_t>(minute_of_day % 60);
        sky.date_time_valid = true;
        sky.ephemeris_model_enabled = true;
        sky.sun_enabled = true;
        sky.moon_enabled = true;
        sky.star_field_enabled = true;
        sky.star_field_intensity = 80.0F;
        sky.set_date(std::chrono::year_month_day{std::chrono::sys_days{config_.date} +
                                                 std::chrono::days{day_offset}});
        add(sky);
    }

    // ---- ownship ----
    {
        const double phase = config_.ownship_speed_mps / config_.circle_radius_m * t;
        const Geodetic g = frame_of_.to_geodetic(on_circle({}, config_.circle_radius_m, phase));
        EntityControl e;
        e.entity_id = ownship_id;
        e.entity_state = EntityState::Active;
        e.entity_type = 101;
        e.alpha = 255;
        e.ground_clamp = GroundClamp::NonConformal; // altitude = height above the terrain
        e.latitude_or_x = g.lat_deg;
        e.longitude_or_y = g.lon_deg;
        e.altitude_or_z = config_.ownship_agl_m;
        e.yaw = static_cast<float>(circle_heading_deg(phase));
        // Coordinated right turn: tan(bank) = v^2 / (g r).
        e.roll =
            static_cast<float>(std::atan(config_.ownship_speed_mps * config_.ownship_speed_mps /
                                         (gravity_mps2 * config_.circle_radius_m)) *
                               rad_to_deg);
        add(e);
    }
    if (frame_ == 0) {
        // The propeller (part 1) is spun by the IG from a single rate, not by per-frame angles.
        add(ArticulatedPartControl{
            .entity_id = ownship_id,
            .articulated_part_id = 1,
            .part_enabled = true,
            .x_offset_enabled = true,
            .z_offset_enabled = true,
            .x_offset = 2.2F,
            .z_offset = 0.0F,
        });
        add(RateControl{
            .entity_id = ownship_id,
            .articulated_part_id = 1,
            .apply_to_articulated_part = true,
            .roll_rate = propeller_roll_rate_dps,
        });
    }

    // ---- convoy ----
    for (int k = 1; k <= 3; ++k) {
        const Geodetic g = frame_of_.to_geodetic(vehicle_enu(k, t));
        EntityControl e;
        e.entity_id = static_cast<std::uint16_t>(k);
        e.entity_state = EntityState::Active;
        e.entity_type = static_cast<std::uint16_t>(199 + k);
        e.alpha = 255;
        e.ground_clamp = GroundClamp::Conformal;
        e.latitude_or_x = g.lat_deg;
        e.longitude_or_y = g.lon_deg;
        e.yaw = static_cast<float>(vehicle_heading_deg(k, t));
        add(e);
    }
    // Turret (part 1) and barrel (part 2) of vehicle 1, absolute angles every frame.
    {
        const auto turret_yaw =
            static_cast<float>(wrap_360(static_cast<double>(turret_yaw_rate_dps) * t));
        const auto barrel_pitch = static_cast<float>(15.0 + (15.0 * std::sin(tau * t / 5.0)));
        add(ArticulatedPartControl{
            .entity_id = 1,
            .articulated_part_id = 1,
            .part_enabled = true,
            .z_offset_enabled = true,
            .yaw_enabled = true,
            .z_offset = -1.4F,
            .yaw = turret_yaw,
        });
        add(ArticulatedPartControl{
            .entity_id = 1,
            .articulated_part_id = 2,
            .part_enabled = true,
            .z_offset_enabled = true,
            .pitch_enabled = true,
            .yaw_enabled = true,
            .z_offset = -1.4F,
            .pitch = barrel_pitch,
            .yaw = turret_yaw,
        });
    }
    if (refresh) {
        add(EntityControl{
            .entity_id = trailer_id,
            .entity_state = EntityState::Active,
            .attach_state = AttachState::Attach,
            .alpha = 255,
            .entity_type = 203,
            .parent_id = 3,
            .latitude_or_x = -8.0,
            .altitude_or_z = 0.0,
        });
    }
    const std::uint32_t detach_at = detach_frame();
    if (frame_ < detach_at) {
        if (refresh) {
            // Cargo riding on vehicle 2, 2.5 m above its reference point (z is down).
            add(EntityControl{
                .entity_id = cargo_id,
                .entity_state = EntityState::Active,
                .attach_state = AttachState::Attach,
                .alpha = 255,
                .entity_type = 150,
                .parent_id = 2,
                .altitude_or_z = -2.5,
            });
        }
    } else if (frame_ == detach_at) {
        // Detach where vehicle 2 is now, then let a rate carry it (no more Entity Control).
        const Geodetic g = frame_of_.to_geodetic(vehicle_enu(2, t));
        add(EntityControl{
            .entity_id = cargo_id,
            .entity_state = EntityState::Active,
            .ground_clamp = GroundClamp::NonConformal,
            .alpha = 255,
            .entity_type = 150,
            .yaw = static_cast<float>(vehicle_heading_deg(2, t)),
            .latitude_or_x = g.lat_deg,
            .longitude_or_y = g.lon_deg,
            .altitude_or_z = cargo_agl_m,
        });
        add(RateControl{
            .entity_id = cargo_id,
            .x_linear_rate = static_cast<float>(cargo_rate_north_mps),
            .y_linear_rate = static_cast<float>(cargo_rate_east_mps),
        });
    }

    // ---- extra movers ----
    for (const Mover& m : extras_) {
        const double phase = m.phase_rad + (m.speed_mps / m.radius_m * t);
        const Geodetic g = frame_of_.to_geodetic(on_circle(m.centre, m.radius_m, phase));
        EntityControl e;
        e.entity_id = m.id;
        e.entity_state = EntityState::Active;
        e.entity_type = m.entity_type;
        e.alpha = 255;
        e.ground_clamp = m.aircraft ? GroundClamp::NonConformal : GroundClamp::Conformal;
        e.latitude_or_x = g.lat_deg;
        e.longitude_or_y = g.lon_deg;
        e.altitude_or_z = m.aircraft ? m.agl_m : 0.0;
        e.yaw = static_cast<float>(circle_heading_deg(phase));
        add(e);
    }

    // ---- view: the pilot's eye in the ownship ----
    if (refresh) {
        add(ViewDefinition{
            .view_id = 0,
            .near_enabled = true,
            .far_enabled = true,
            .left_enabled = true,
            .right_enabled = true,
            .top_enabled = true,
            .bottom_enabled = true,
            .near_plane = 1.0F,
            .far_plane = 20000.0F,
            .left = -35.0F,
            .right = 35.0F,
            .top = 21.0F,
            .bottom = -21.0F,
        });
        add(ViewControl{
            .view_id = 0,
            .x_offset_enabled = true,
            .y_offset_enabled = true,
            .z_offset_enabled = true,
            .roll_enabled = true,
            .pitch_enabled = true,
            .yaw_enabled = true,
            .entity_id = ownship_id,
            .x_offset = 1.0F,
            .z_offset = -1.0F,
            // The ownship banks ~23 degrees into its clockwise turn, so its right wing points
            // below the horizon: yaw 75 (convoy is a little ahead of the right wing) and pitch
            // +12 (cancelling the bank) frame the convoy ~11 degrees below the horizon.
            .pitch = 12.0F,
            .yaw = 75.0F,
        });
    }

    // ---- HAT/HOT requests ----
    const double origin_lat = config_.origin.lat_deg;
    const double origin_lon = config_.origin.lon_deg;
    if (frame_ == 0) {
        add(HatHotRequest{
            .hat_hot_id = 3,
            .request_type = HatHotRequestType::Extended,
            .coordinate_system = HatHotCoordinateSystem::Entity,
            .update_period = 30,
            .entity_id = ownship_id,
        });
        add(HatHotRequest{
            .hat_hot_id = 4,
            .request_type = HatHotRequestType::Hat,
            .coordinate_system = HatHotCoordinateSystem::Entity,
            .update_period = 15,
            .entity_id = 1,
            .altitude_or_z = -2.0,
        });
    }
    if (frame_ == 1) {
        add(HatHotRequest{
            .hat_hot_id = 1,
            .request_type = HatHotRequestType::Hat,
            .latitude_or_x = origin_lat,
            .longitude_or_y = origin_lon,
            .altitude_or_z = 500.0,
        });
    }
    if (frame_ == 30) {
        const Geodetic east = frame_of_.to_geodetic({300.0, 120.0, 0.0});
        add(HatHotRequest{
            .hat_hot_id = 2,
            .request_type = HatHotRequestType::Hot,
            .latitude_or_x = east.lat_deg,
            .longitude_or_y = east.lon_deg,
        });
    }
    if (frame_ == 90) {
        add(HatHotRequest{
            .hat_hot_id = 5,
            .request_type = HatHotRequestType::Hat,
            .latitude_or_x = origin_lat + 2.0, // far outside the mock database
            .longitude_or_y = origin_lon,
            .altitude_or_z = 100.0,
        });
    }
    if (failure) {
        return std::unexpected(*failure);
    }
    ++frame_;
    return {};
}

Result<std::span<const std::byte>> ScriptedHost::next_message() {
    MessageBuilder builder(Direction::HostToIg, storage_);
    if (auto r = build_frame(builder); !r) {
        return std::unexpected(r.error());
    }
    return builder.bytes();
}

} // namespace cigi::mock
