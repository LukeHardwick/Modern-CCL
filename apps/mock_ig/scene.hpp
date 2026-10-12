#pragma once

// The mock IG's world model: what the Host has told the IG so far, resolved into poses.
//
// Feed it Host -> IG packets (directly through apply(), or through register_handlers() and a
// Dispatcher), call advance(dt) once per IG frame, then read entities(), views(), celestial() and
// the history (trails, HAT/HOT probes, log). Everything the ICD says about each packet is cited
// at its handler in scene.cpp.
//
// Reading poses: handlers only change the *commanded* state and mark the scene dirty; resolve()
// (called by advance()) turns it into world poses. Call resolve() before reading `world` data
// after applying packets without advancing.

#include "mock_ig/geo.hpp"
#include "mock_ig/terrain.hpp"

#include <array>
#include <chrono>
#include <cigi/dispatch.hpp>
#include <cigi/packets.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace cigi::mock {

class HatHotResponder;

/// How the viewer draws an entity. Entity Type values are database-specific in CIGI (ICD 4.1.2);
/// this mock IG has no database, so it uses its own convention: 0 none, 1-99 other, 100-199
/// aircraft, 200-299 ground vehicle, anything else other.
enum class ShapeClass : std::uint8_t {
    None,
    Aircraft,
    GroundVehicle,
    Other,
};
[[nodiscard]] ShapeClass classify_entity_type(std::uint16_t entity_type) noexcept;

/// Pose and rates of one articulated part (ICD 4.1.6, 4.1.8). The submodel frame is taken to be
/// the entity's body frame (the ICD leaves it model-defined, 3.4.3): `offset` is metres from the
/// entity reference point along the body axes and `attitude` is the part's rotation in that
/// frame. Values are absolute, never cumulative.
struct PartState {
    bool enabled = true; ///< Default is IG-configurable (ICD 4.1.6); this IG shows parts.
    Vec3d offset;
    Attitude attitude;
    Vec3d linear_rate;     ///< m/s along the part axes (Rate Control).
    Attitude angular_rate; ///< deg/s; yaw_deg is the yaw rate and so on.
};

/// Rate Control state of an entity (ICD 4.1.8).
struct RateState {
    Vec3d linear;     ///< m/s; frame given by `frame`.
    Attitude angular; ///< deg/s, applied to the Euler angles (yaw_deg is the yaw rate).
    RateCoordinateSystem frame = RateCoordinateSystem::WorldParent;

    [[nodiscard]] bool active() const noexcept {
        return linear != Vec3d{} || angular != Attitude{};
    }
};

/// Latest Component Control data for one (class, component, instance) (ICD 4.1.4); stored, not
/// interpreted.
struct ComponentState {
    std::uint8_t state = 0;
    std::array<std::uint32_t, 6> data{};
};
using ComponentKey =
    std::tuple<std::uint8_t, std::uint16_t, std::uint16_t>; ///< class, id, instance

struct Entity {
    std::uint16_t id = 0;
    std::uint16_t entity_type = 0;
    EntityState state = EntityState::InactiveStandby;
    std::uint8_t alpha = 255; ///< Explicit alpha, 255 opaque.
    bool inherit_alpha = false;
    GroundClamp ground_clamp = GroundClamp::NoClamp; ///< Ignored while attached.
    bool attached = false;
    std::uint16_t parent_id = 0;
    EntityControl last_control; ///< The most recent Entity Control packet, verbatim.

    // ---- commanded state (Entity Control / Rate Control / Articulated Part Control) ----
    Geodetic commanded; ///< Detached: lat, lon, and altitude (an offset when clamped).
    Vec3d offset;       ///< Attached: metres in the parent's body frame.
    Attitude attitude;  ///< Degrees; relative to the parent when attached.
    RateState rates;    ///< Entity rates (ICD 4.1.8); zero means stationary.
    std::map<std::uint8_t, PartState> parts;

    // ---- resolved by SceneModel::resolve() ----
    bool visible = false; ///< Active, and so is every ancestor.
    std::uint8_t apparent_alpha = 255;
    Geodetic position;       ///< Resolved geodetic position (altitude above MSL).
    Vec3d enu;               ///< Position in the scene's local frame.
    Mat3 body_to_enu;        ///< Body (x fwd, y right, z down) to ENU.
    Attitude world_attitude; ///< World attitude (yaw 0..360 from north, pitch, roll).
    double terrain_m = 0;    ///< Terrain height below the entity.
    double agl_m = 0;        ///< Height above that terrain.

    std::deque<Vec3d> trail;         ///< Recent positions (ENU), oldest first.
    std::uint64_t resolve_stamp = 0; ///< Bookkeeping of SceneModel::resolve().
};

/// One view (ICD 4.1.16, 4.1.21). Defaults describe an IG-chosen forward view, since a Host may
/// send View Control before any View Definition.
struct ViewState {
    std::uint16_t view_id = 0;
    std::uint8_t group_id = 0;
    bool controlled = false; ///< A View Control for this view has arrived.
    std::uint16_t entity_id = 0;
    Vec3d offset;      ///< Metres from the entity reference point, entity body axes.
    Attitude attitude; ///< Degrees relative to the entity body axes.
    // View Definition (volume in metres, half-angles in degrees).
    float near_plane = 1.0F;
    float far_plane = 20000.0F;
    float left = -35.0F;
    float right = 35.0F;
    float top = 21.0F;
    float bottom = -21.0F;
    ProjectionType projection = ProjectionType::Perspective;
    MirrorMode mirror = MirrorMode::None;
    PixelReplicationMode pixel_replication = PixelReplicationMode::None;
    std::uint8_t view_type = 0;
    bool reorder = false;

    [[nodiscard]] double horizontal_fov_deg() const noexcept {
        return static_cast<double>(right) - static_cast<double>(left);
    }
    [[nodiscard]] double vertical_fov_deg() const noexcept {
        return static_cast<double>(top) - static_cast<double>(bottom);
    }
};

/// Celestial Sphere Control state (ICD 4.1.9).
struct CelestialState {
    bool received = false;  ///< False until the first Celestial Sphere Control.
    std::uint8_t hour = 12; ///< UTC.
    std::uint8_t minute = 0;
    bool ephemeris_model = true;
    bool sun = true;
    bool moon = false;
    bool star_field = false;
    bool date_time_valid = false;
    std::optional<std::chrono::year_month_day> date = std::chrono::year_month_day{
        std::chrono::year{2026}, std::chrono::June, std::chrono::day{21}};
    float star_field_intensity = 0.0F;

    [[nodiscard]] double utc_hours() const noexcept { return hour + (minute / 60.0); }
};

/// A HAT/HOT request together with what the IG answered (kept for display).
struct HatHotProbe {
    std::uint64_t ig_frame = 0;
    HatHotRequest request;
    bool periodic = false;
    bool valid = false;
    Geodetic point; ///< The test point (altitude is the requested one; HOT ignores it).
    double hat_m = 0;
    double hot_m = 0;
    Material material = Material::Grass;
    SurfaceNormal normal;
};

/// Counters for things the model had to ignore or could not honour.
struct SceneStats {
    std::uint64_t packets = 0; ///< Packets applied through apply().
    std::uint64_t entities_created = 0;
    std::uint64_t entities_destroyed = 0;
    std::uint64_t unknown_entity = 0;  ///< Articulated Part / Rate Control for an unknown entity.
    std::uint64_t invalid_parent = 0;  ///< Entity Control attach with a missing / cyclic parent.
    std::uint64_t destroy_unknown = 0; ///< Destroy for an entity that does not exist.
    std::uint64_t component_controls = 0;
    std::uint64_t unknown_packets = 0; ///< Opcodes the model does not know (dispatcher hook).
    std::uint64_t decode_errors = 0;   ///< Body / header errors (dispatcher hook).
};

struct SceneConfig {
    Geodetic origin{51.5, -0.12, 0.0}; ///< Origin of the local ENU frame.
    DatabaseBounds bounds = DatabaseBounds::around(origin, 0.25);
    std::size_t trail_length = 600; ///< Points kept per entity.
    double trail_step_m = 2.0;      ///< A trail point is added after moving this far.
    std::size_t probe_history = 64;
    std::size_t log_history = 200;
};

class SceneModel {
public:
    using Handlers = Dispatcher<HostToIgPackets>;

    explicit SceneModel(const SceneConfig& config = {});

    // ---- applying Host -> IG packets ----
    void apply(const IgControl& p);
    void apply(const EntityControl& p);
    void apply(const ArticulatedPartControl& p);
    void apply(const RateControl& p);
    void apply(const ComponentControl& p);
    void apply(const CelestialSphereControl& p);
    void apply(const ViewControl& p);
    void apply(const ViewDefinition& p);
    void apply(const HatHotRequest& p);

    /// Wire every Host -> IG packet type into this model. If `responder` is given, HAT/HOT
    /// requests are also handed to it. The scene (and responder) must outlive the dispatcher.
    /// The dispatcher's unknown-opcode and error hooks are left alone (they may belong to a
    /// facade): forward them to note_unknown() / note_error(), or use register_all_handlers().
    void register_handlers(Handlers& dispatcher, HatHotResponder* responder = nullptr);
    /// register_handlers() plus the unknown-opcode and error hooks, for a raw Dispatcher.
    void register_all_handlers(Handlers& dispatcher, HatHotResponder* responder = nullptr);

    /// Count (and log) a packet with an unknown opcode / a decode error.
    void note_unknown(const UnknownPacket& u);
    void note_error(const Error& e);

    // ---- time ----
    /// Integrate Rate Control over `dt` seconds (entities and parts that are Active), resolve
    /// poses and record trails. Call once per IG frame.
    void advance(double dt_s);
    /// Recompute world poses from the commanded state (idempotent while nothing changed).
    void resolve();

    // ---- reading the model ----
    [[nodiscard]] const std::map<std::uint16_t, Entity>& entities() const noexcept {
        return entities_;
    }
    [[nodiscard]] const Entity* find(std::uint16_t id) const noexcept;
    [[nodiscard]] const std::map<std::uint16_t, ViewState>& views() const noexcept {
        return views_;
    }
    [[nodiscard]] const ViewState* find_view(std::uint16_t id) const noexcept;
    /// Eye pose of a view: the attached entity's pose composed with the view's offset and
    /// attitude (ICD 4.1.16: translate along the entity axes, then rotate). Empty if the view
    /// is unknown or its entity does not exist.
    [[nodiscard]] std::optional<ChildPose> view_pose(std::uint16_t view_id) const;
    [[nodiscard]] const CelestialState& celestial() const noexcept { return celestial_; }
    [[nodiscard]] const std::map<ComponentKey, ComponentState>& components() const noexcept {
        return components_;
    }
    [[nodiscard]] const IgControl& ig_control() const noexcept { return ig_control_; }
    [[nodiscard]] std::uint32_t host_frame_number() const noexcept {
        return ig_control_.host_frame_number;
    }
    [[nodiscard]] const SceneStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const SceneConfig& config() const noexcept { return config_; }
    [[nodiscard]] const LocalFrame& frame() const noexcept { return frame_; }
    [[nodiscard]] std::uint64_t frame_count() const noexcept { return frame_count_; }
    [[nodiscard]] double sim_time_s() const noexcept { return sim_time_s_; }

    /// Entity IDs of `id`'s direct children, ascending.
    [[nodiscard]] std::vector<std::uint16_t> children_of(std::uint16_t id) const;

    // ---- history ----
    [[nodiscard]] const std::deque<HatHotProbe>& probes() const noexcept { return probes_; }
    void add_probe(const HatHotProbe& probe);
    /// HAT/HOT requests as received, newest last (bounded).
    [[nodiscard]] const std::deque<HatHotRequest>& recent_requests() const noexcept {
        return recent_requests_;
    }
    [[nodiscard]] const std::deque<std::string>& log() const noexcept { return log_; }
    /// Also receive every log line as it is written (e.g. to print it).
    void set_log_sink(std::function<void(std::string_view)> sink) { log_sink_ = std::move(sink); }

private:
    void log_line(std::string text);
    void destroy_tree(std::uint16_t id);
    [[nodiscard]] bool active_chain(const Entity& e) const;
    [[nodiscard]] bool would_cycle(std::uint16_t entity_id, std::uint16_t parent_id) const;
    void resolve_entity(Entity& e);
    void record_trails();
    ViewState& view_for(std::uint16_t id);

    SceneConfig config_;
    LocalFrame frame_;
    std::map<std::uint16_t, Entity> entities_;
    std::map<std::uint16_t, ViewState> views_;
    std::map<ComponentKey, ComponentState> components_;
    CelestialState celestial_;
    IgControl ig_control_;
    SceneStats stats_;
    std::deque<HatHotProbe> probes_;
    std::deque<HatHotRequest> recent_requests_;
    std::deque<std::string> log_;
    std::function<void(std::string_view)> log_sink_;
    std::uint64_t resolve_epoch_ = 0;
    std::set<std::uint8_t> seen_unknown_opcodes_;
    std::uint64_t frame_count_ = 0;
    double sim_time_s_ = 0;
    bool dirty_ = true;
};

} // namespace cigi::mock
