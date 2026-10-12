#pragma once

// An in-process Host that produces a realistic message per frame, for the viewer's offline mode
// and as an end-to-end test of codec + dispatch + scene. Every message is built with
// MessageBuilder, so it goes through the real encoder; MockIg::ingest() then parses and
// dispatches it like a datagram from a network Host.
//
// The scenario (t = frame * dt, frame counted from 0):
//   entity 0   ownship, type 101 (aircraft): flies a clockwise circle of radius 1500 m around the
//              origin at 80 m/s, held 300 m above the terrain by Ground Clamp (altitude is the
//              offset), banked for a coordinated turn. Part 1 is a propeller spun only by a Rate
//              Control packet sent in frame 0.
//   entities 1-3  ground vehicles (types 200-202) driving a loop, Conformal clamp. Entity 1 has an
//              articulated turret (part 1, yaw sweeps 36 deg/s) and barrel (part 2, pitch).
//   entity 4   trailer (type 203), attached 8 m behind vehicle 3.
//   entity 5   cargo (type 150), attached above vehicle 2; detached at `detach_s` (20 s) and then
//              moved only by a Rate Control packet (8 m/s north, 4 m/s east, 30 m above ground).
//   view 0     attached to the ownship at the pilot's eye, 70 x 42 degrees.
//   celestial  UTC time-of-day advances one minute per second from `start_utc_hours`.
//   HAT/HOT    one-shot HAT (id 1, frame 1), one-shot HOT (id 2, frame 30), periodic extended
//              request relative to the ownship (id 3, every 30 frames), periodic HAT under vehicle
//              1 (id 4, every 15 frames) and one-shot outside the database (id 5, frame 90).
//   entities 100+  `extra_entities` additional movers (for load testing).

#include "mock_ig/geo.hpp"

#include <chrono>
#include <cigi/message/builder.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cigi::mock {

struct ScriptedHostConfig {
    Geodetic origin{51.5, -0.12, 0.0}; ///< Centre of the scenario (use the scene's origin).
    double dt_s = 1.0 / 60.0;
    double circle_radius_m = 1500.0;
    double ownship_speed_mps = 80.0;
    double ownship_agl_m = 300.0;
    double detach_s = 20.0; ///< When entity 5 leaves vehicle 2.
    double start_utc_hours = 10.0;
    std::chrono::year_month_day date{std::chrono::year{2026}, std::chrono::June,
                                     std::chrono::day{21}};
    std::uint16_t extra_entities = 0; ///< Entities 100 .. 100 + n - 1.
};

class ScriptedHost {
public:
    static constexpr std::uint16_t first_extra_id = 100;

    explicit ScriptedHost(const ScriptedHostConfig& config = {});

    /// Add the next frame's packets to `builder` (which must be empty and HostToIg) and advance
    /// to the following frame. Fails like MessageBuilder::add (e.g. BufferTooSmall).
    [[nodiscard]] Result<void> build_frame(MessageBuilder& builder);

    /// Build the next frame into an internal buffer; valid until the next call.
    [[nodiscard]] Result<std::span<const std::byte>> next_message();

    /// Number of frames built so far (the next frame's Host Frame Number).
    [[nodiscard]] std::uint32_t frame() const noexcept { return frame_; }
    [[nodiscard]] const ScriptedHostConfig& config() const noexcept { return config_; }

    // ---- the scenario's ground truth, for tests and for judging the viewer ----
    /// Ownship position in the local ENU frame at time `t_s` (altitude excluded: z is 0).
    [[nodiscard]] Vec3d ownship_enu(double t_s) const noexcept;
    /// Ownship heading (degrees clockwise from north) at time `t_s`.
    [[nodiscard]] double ownship_heading_deg(double t_s) const noexcept;
    /// Position / heading of ground vehicle `index` (1..3) at `t_s` (ENU, z = 0).
    [[nodiscard]] Vec3d vehicle_enu(int index, double t_s) const noexcept;
    [[nodiscard]] double vehicle_heading_deg(int index, double t_s) const noexcept;
    /// Entities the scenario keeps alive (ownship, 5 convoy entities, extras).
    [[nodiscard]] std::size_t entity_count() const noexcept {
        return 6 + static_cast<std::size_t>(config_.extra_entities);
    }
    /// Frame in which entity 5 detaches.
    [[nodiscard]] std::uint32_t detach_frame() const noexcept;

private:
    struct Mover {
        std::uint16_t id = 0;
        std::uint16_t entity_type = 0;
        bool aircraft = false;
        Vec3d centre; // ENU
        double radius_m = 0;
        double speed_mps = 0;
        double phase_rad = 0;
        double agl_m = 0;
    };

    ScriptedHostConfig config_;
    LocalFrame frame_of_;
    std::vector<Mover> extras_;
    std::vector<std::byte> storage_;
    std::uint32_t frame_ = 0;
};

} // namespace cigi::mock
