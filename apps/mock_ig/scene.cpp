#include "mock_ig/scene.hpp"

#include "mock_ig/hat_hot.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace cigi::mock {

namespace {

constexpr double min_lon_scale = 1.0; // m per degree; keeps division finite at the poles

/// Add `rate * dt` to each Euler angle and bring the result back into its legal range.
void step_attitude(Attitude& a, const Attitude& rate, double dt_s) noexcept {
    a.yaw_deg = wrap_360(a.yaw_deg + (rate.yaw_deg * dt_s));
    a.pitch_deg = std::clamp(a.pitch_deg + (rate.pitch_deg * dt_s), -90.0, 90.0);
    a.roll_deg = wrap_180(a.roll_deg + (rate.roll_deg * dt_s));
}

Attitude attitude_of(float yaw, float pitch, float roll) noexcept {
    return {static_cast<double>(yaw), static_cast<double>(pitch), static_cast<double>(roll)};
}

} // namespace

ShapeClass classify_entity_type(std::uint16_t entity_type) noexcept {
    if (entity_type == 0) {
        return ShapeClass::None;
    }
    if (entity_type >= 100 && entity_type <= 199) {
        return ShapeClass::Aircraft;
    }
    if (entity_type >= 200 && entity_type <= 299) {
        return ShapeClass::GroundVehicle;
    }
    return ShapeClass::Other;
}

SceneModel::SceneModel(const SceneConfig& config) : config_(config), frame_(config.origin) {}

void SceneModel::log_line(std::string text) {
    if (log_sink_) {
        log_sink_(text);
    }
    log_.push_back(std::move(text));
    while (log_.size() > config_.log_history) {
        log_.pop_front();
    }
}

// ---- IG Control (ICD 4.1.1) -------------------------------------------------------------------

void SceneModel::apply(const IgControl& p) {
    ++stats_.packets;
    ig_control_ = p;
}

// ---- Entity Control (ICD 4.1.2) ---------------------------------------------------------------

bool SceneModel::would_cycle(std::uint16_t entity_id, std::uint16_t parent_id) const {
    // Walk up from the proposed parent; the chain is at most as long as the entity table.
    std::uint16_t cursor = parent_id;
    for (std::size_t steps = 0; steps <= entities_.size(); ++steps) {
        if (cursor == entity_id) {
            return true;
        }
        const Entity* e = find(cursor);
        if (e == nullptr || !e->attached) {
            return false;
        }
        cursor = e->parent_id;
    }
    return true; // longer than the table: an existing loop
}

void SceneModel::destroy_tree(std::uint16_t id) {
    // "Any children are also destroyed" (ICD 4.1.2, Entity State). Rates die with the entity.
    for (const std::uint16_t child : children_of(id)) {
        destroy_tree(child);
        ++stats_.entities_destroyed;
    }
    entities_.erase(id);
}

void SceneModel::apply(const EntityControl& p) {
    ++stats_.packets;
    dirty_ = true;
    if (p.entity_state == EntityState::Destroyed) {
        // All other fields are ignored (Table 7, Entity State).
        if (find(p.entity_id) == nullptr) {
            ++stats_.destroy_unknown;
            return;
        }
        destroy_tree(p.entity_id);
        ++stats_.entities_destroyed;
        log_line(std::format("entity {} destroyed", p.entity_id));
        return;
    }
    const bool attach = p.attach_state == AttachState::Attach;
    if (attach && (p.parent_id == p.entity_id || find(p.parent_id) == nullptr ||
                   would_cycle(p.entity_id, p.parent_id))) {
        // "If the specified parent entity is invalid, no change in the attachment will be made"
        // (Table 7, Parent ID). The mock disregards the whole packet: its position fields are
        // offsets in a frame that does not exist.
        ++stats_.invalid_parent;
        log_line(
            std::format("entity {}: invalid parent {}, packet ignored", p.entity_id, p.parent_id));
        return;
    }
    auto [it, created] = entities_.try_emplace(p.entity_id);
    Entity& e = it->second;
    if (created) {
        e.id = p.entity_id;
        ++stats_.entities_created;
        log_line(std::format("entity {} created (type {})", p.entity_id, p.entity_type));
    }
    e.last_control = p;
    e.state = p.entity_state;
    e.entity_type = p.entity_type;
    e.alpha = p.alpha;
    e.inherit_alpha = p.inherit_alpha;
    e.ground_clamp = p.ground_clamp;
    e.attached = attach;
    e.parent_id = attach ? p.parent_id : std::uint16_t{0};
    e.attitude = attitude_of(p.yaw, p.pitch, p.roll);
    if (attach) {
        // Child positions are offsets in the parent's NED body frame (ICD 4.1.2, 3.4.2.1).
        e.offset = {p.latitude_or_x, p.longitude_or_y, p.altitude_or_z};
    } else {
        e.commanded = {p.latitude_or_x, p.longitude_or_y, p.altitude_or_z};
    }
}

// ---- Articulated Part Control (ICD 4.1.6) -----------------------------------------------------

void SceneModel::apply(const ArticulatedPartControl& p) {
    ++stats_.packets;
    auto it = entities_.find(p.entity_id);
    if (it == entities_.end()) {
        ++stats_.unknown_entity;
        log_line(std::format("articulated part {} for unknown entity {}", p.articulated_part_id,
                             p.entity_id));
        return;
    }
    PartState& part = it->second.parts[p.articulated_part_id];
    part.enabled = p.part_enabled;
    // A disabled component keeps its previous value (ICD 4.1.6, enable flags). Values are
    // absolute, not cumulative (3.4.3).
    if (p.x_offset_enabled) {
        part.offset.x = static_cast<double>(p.x_offset);
    }
    if (p.y_offset_enabled) {
        part.offset.y = static_cast<double>(p.y_offset);
    }
    if (p.z_offset_enabled) {
        part.offset.z = static_cast<double>(p.z_offset);
    }
    if (p.roll_enabled) {
        part.attitude.roll_deg = static_cast<double>(p.roll);
    }
    if (p.pitch_enabled) {
        part.attitude.pitch_deg = static_cast<double>(p.pitch);
    }
    if (p.yaw_enabled) {
        part.attitude.yaw_deg = static_cast<double>(p.yaw);
    }
}

// ---- Rate Control (ICD 4.1.8) -----------------------------------------------------------------

void SceneModel::apply(const RateControl& p) {
    ++stats_.packets;
    auto it = entities_.find(p.entity_id);
    if (it == entities_.end()) {
        ++stats_.unknown_entity;
        log_line(std::format("rate control for unknown entity {}", p.entity_id));
        return;
    }
    const Vec3d linear{static_cast<double>(p.x_linear_rate), static_cast<double>(p.y_linear_rate),
                       static_cast<double>(p.z_linear_rate)};
    const Attitude angular = attitude_of(p.yaw_rate, p.pitch_rate, p.roll_rate);
    if (p.apply_to_articulated_part) {
        // Rates of a part are in the submodel frame; the coordinate-system bit is ignored.
        PartState& part = it->second.parts[p.articulated_part_id];
        part.linear_rate = linear;
        part.angular_rate = angular;
    } else {
        it->second.rates = {linear, angular, p.coordinate_system};
    }
}

// ---- Component Control (ICD 4.1.4) ------------------------------------------------------------

void SceneModel::apply(const ComponentControl& p) {
    ++stats_.packets;
    ++stats_.component_controls;
    const ComponentKey key{static_cast<std::uint8_t>(p.component_class), p.component_id,
                           p.instance_id};
    const ComponentState next{p.component_state,
                              {p.data_1, p.data_2, p.data_3, p.data_4, p.data_5, p.data_6}};
    auto [it, inserted] = components_.try_emplace(key, next);
    if (inserted || it->second.state != next.state || it->second.data != next.data) {
        it->second = next;
        // Logged on change only: a Host may repeat the packet every frame.
        log_line(std::format("component class {} id {} instance {} state {}",
                             static_cast<unsigned>(p.component_class), p.component_id,
                             p.instance_id, p.component_state));
    }
}

// ---- Celestial Sphere Control (ICD 4.1.9) -----------------------------------------------------

void SceneModel::apply(const CelestialSphereControl& p) {
    ++stats_.packets;
    celestial_.received = true;
    celestial_.ephemeris_model = p.ephemeris_model_enabled;
    celestial_.sun = p.sun_enabled;
    celestial_.moon = p.moon_enabled;
    celestial_.star_field = p.star_field_enabled;
    celestial_.star_field_intensity = p.star_field_intensity;
    celestial_.date_time_valid = p.date_time_valid;
    if (p.date_time_valid) {
        // Hour, Minute (UTC, Appendix B) and Date override the IG's own clock only when valid.
        celestial_.hour = p.hour;
        celestial_.minute = p.minute;
        if (auto d = p.calendar_date()) {
            celestial_.date = *d;
        }
    }
}

// ---- View Control / View Definition (ICD 4.1.16, 4.1.21) --------------------------------------

ViewState& SceneModel::view_for(std::uint16_t id) {
    auto [it, created] = views_.try_emplace(id);
    if (created) {
        it->second.view_id = id;
    }
    return it->second;
}

void SceneModel::apply(const ViewControl& p) {
    ++stats_.packets;
    const auto update = [&p](ViewState& v) {
        v.controlled = true;
        v.entity_id = p.entity_id;
        if (p.x_offset_enabled) {
            v.offset.x = static_cast<double>(p.x_offset);
        }
        if (p.y_offset_enabled) {
            v.offset.y = static_cast<double>(p.y_offset);
        }
        if (p.z_offset_enabled) {
            v.offset.z = static_cast<double>(p.z_offset);
        }
        if (p.roll_enabled) {
            v.attitude.roll_deg = static_cast<double>(p.roll);
        }
        if (p.pitch_enabled) {
            v.attitude.pitch_deg = static_cast<double>(p.pitch);
        }
        if (p.yaw_enabled) {
            v.attitude.yaw_deg = static_cast<double>(p.yaw);
        }
    };
    if (p.group_id != 0) {
        // A non-zero Group ID addresses every view of the group; View ID is ignored.
        for (auto& [id, v] : views_) {
            if (v.group_id == p.group_id) {
                update(v);
            }
        }
    } else {
        update(view_for(p.view_id));
    }
}

void SceneModel::apply(const ViewDefinition& p) {
    ++stats_.packets;
    ViewState& v = view_for(p.view_id);
    v.group_id = p.group_id;
    v.projection = p.projection_type;
    v.mirror = p.mirror_mode;
    v.pixel_replication = p.pixel_replication_mode;
    v.view_type = p.view_type;
    v.reorder = p.reorder;
    // Each volume parameter applies only when its enable flag is set.
    if (p.near_enabled) {
        v.near_plane = p.near_plane;
    }
    if (p.far_enabled) {
        v.far_plane = p.far_plane;
    }
    if (p.left_enabled) {
        v.left = p.left;
    }
    if (p.right_enabled) {
        v.right = p.right;
    }
    if (p.top_enabled) {
        v.top = p.top;
    }
    if (p.bottom_enabled) {
        v.bottom = p.bottom;
    }
}

// ---- HAT/HOT Request (ICD 4.1.24) -------------------------------------------------------------

void SceneModel::apply(const HatHotRequest& p) {
    ++stats_.packets;
    recent_requests_.push_back(p);
    while (recent_requests_.size() > config_.probe_history) {
        recent_requests_.pop_front();
    }
}

void SceneModel::add_probe(const HatHotProbe& probe) {
    probes_.push_back(probe);
    while (probes_.size() > config_.probe_history) {
        probes_.pop_front();
    }
}

void SceneModel::register_handlers(Handlers& dispatcher, HatHotResponder* responder) {
    dispatcher.on<IgControl>([this](const IgControl& p) { apply(p); });
    dispatcher.on<EntityControl>([this](const EntityControl& p) { apply(p); });
    dispatcher.on<ComponentControl>([this](const ComponentControl& p) { apply(p); });
    dispatcher.on<ArticulatedPartControl>([this](const ArticulatedPartControl& p) { apply(p); });
    dispatcher.on<RateControl>([this](const RateControl& p) { apply(p); });
    dispatcher.on<CelestialSphereControl>([this](const CelestialSphereControl& p) { apply(p); });
    dispatcher.on<ViewControl>([this](const ViewControl& p) { apply(p); });
    dispatcher.on<ViewDefinition>([this](const ViewDefinition& p) { apply(p); });
    dispatcher.on<HatHotRequest>([this, responder](const HatHotRequest& p) {
        apply(p);
        if (responder != nullptr) {
            responder->submit(p);
        }
    });
    dispatcher.on_unknown([this](const UnknownPacket& u) {
        ++stats_.unknown_packets;
        // ICD 2.11 rule 3: unknown opcodes are ignored. Log each opcode once.
        if (seen_unknown_opcodes_.insert(u.opcode).second) {
            log_line(std::format("ignoring unknown opcode {}", u.opcode));
        }
    });
    dispatcher.on_error([this](const Error& e) {
        ++stats_.decode_errors;
        log_line(std::format("decode error: {} (opcode {}, offset {})", to_string(e.code), e.opcode,
                             e.offset));
    });
}

// ---- time -------------------------------------------------------------------------------------

void SceneModel::integrate(Entity& e, double dt_s) const {
    if (e.rates.active()) {
        // Linear rates (ICD 4.1.8). Top-level, World/Parent: along the surface, x north, y east,
        // z down (the NED reference axes of 3.4.1.2). Child, World/Parent: the parent's body
        // axes. Local: the entity's own body axes, turned into the frame above by its attitude.
        Vec3d v = e.rates.linear;
        if (e.rates.frame == RateCoordinateSystem::Local) {
            v = euler_zyx(e.attitude) * v;
        }
        if (e.attached) {
            e.offset = e.offset + (v * dt_s);
        } else {
            const double lat = e.commanded.lat_deg;
            e.commanded.lat_deg += v.x * dt_s / meters_per_degree_lat(lat);
            e.commanded.lon_deg += v.y * dt_s / std::max(meters_per_degree_lon(lat), min_lon_scale);
            e.commanded.alt_m -= v.z * dt_s; // z is down
        }
        // The ICD defines the angular rates as rates of the yaw, pitch and roll angles.
        step_attitude(e.attitude, e.rates.angular, dt_s);
    }
    for (auto& [part_id, part] : e.parts) {
        part.offset = part.offset + (part.linear_rate * dt_s);
        step_attitude(part.attitude, part.angular_rate, dt_s);
    }
}

void SceneModel::advance(double dt_s) {
    if (dt_s > 0.0) {
        for (auto& [id, e] : entities_) {
            // An Inactive/Standby entity has no transformations applied (Table 7).
            if (e.state == EntityState::Active) {
                integrate(e, dt_s);
            }
        }
        dirty_ = true;
    }
    resolve();
    ++frame_count_;
    sim_time_s_ += dt_s;
    record_trails();
}

void SceneModel::resolve_entity(Entity& e) {
    if (e.resolve_stamp == resolve_epoch_) {
        return;
    }
    e.resolve_stamp = resolve_epoch_;
    if (e.attached) {
        // The parent always exists: destroying it destroys its children, and attaching to a
        // missing parent is refused.
        Entity& parent = entities_.at(e.parent_id);
        resolve_entity(parent);
        const ChildPose pose = compose_child(parent.enu, parent.body_to_enu, e.offset, e.attitude);
        e.enu = pose.position_enu;
        e.body_to_enu = pose.body_to_enu;
        e.position = frame_.to_geodetic(e.enu);
        e.visible = e.state == EntityState::Active && parent.visible;
        // Combined alpha (ICD 4.1.2, Inherit Alpha): a = a0 * ap / 255.
        e.apparent_alpha = e.inherit_alpha
                               ? static_cast<std::uint8_t>((e.alpha * parent.apparent_alpha) / 255)
                               : e.alpha;
        e.terrain_m = terrain_height(e.position.lat_deg, e.position.lon_deg);
    } else {
        Geodetic g = e.commanded;
        e.terrain_m = terrain_height(g.lat_deg, g.lon_deg);
        if (e.ground_clamp != GroundClamp::NoClamp) {
            g.alt_m += e.terrain_m; // altitude is an offset above the surface (Table 7)
        }
        e.position = g;
        e.enu = frame_.to_enu(g);
        e.body_to_enu =
            e.ground_clamp == GroundClamp::Conformal
                ? conformal_body_to_enu(e.attitude, terrain_normal(g.lat_deg, g.lon_deg).enu)
                : cigi::mock::body_to_enu(e.attitude);
        e.visible = e.state == EntityState::Active;
        e.apparent_alpha = e.alpha;
    }
    e.agl_m = e.position.alt_m - e.terrain_m;
    e.world_attitude = attitude_from_body_to_enu(e.body_to_enu);
}

void SceneModel::resolve() {
    if (!dirty_) {
        return;
    }
    ++resolve_epoch_;
    for (auto& [id, e] : entities_) {
        resolve_entity(e);
    }
    dirty_ = false;
}

void SceneModel::record_trails() {
    for (auto& [id, e] : entities_) {
        if (!e.visible) {
            continue;
        }
        if (e.trail.empty() || length(e.enu - e.trail.back()) >= config_.trail_step_m) {
            e.trail.push_back(e.enu);
            while (e.trail.size() > config_.trail_length) {
                e.trail.pop_front();
            }
        }
    }
}

// ---- reading ----------------------------------------------------------------------------------

const Entity* SceneModel::find(std::uint16_t id) const noexcept {
    const auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
}

const ViewState* SceneModel::find_view(std::uint16_t id) const noexcept {
    const auto it = views_.find(id);
    return it == views_.end() ? nullptr : &it->second;
}

std::optional<ChildPose> SceneModel::view_pose(std::uint16_t view_id) const {
    const ViewState* v = find_view(view_id);
    if (v == nullptr) {
        return std::nullopt;
    }
    const Entity* e = find(v->entity_id);
    if (e == nullptr) {
        return std::nullopt;
    }
    // ICD 4.1.16: translate along the entity axes, then rotate (yaw, pitch, roll).
    return compose_child(e->enu, e->body_to_enu, v->offset, v->attitude);
}

std::vector<std::uint16_t> SceneModel::children_of(std::uint16_t id) const {
    std::vector<std::uint16_t> out;
    for (const auto& [child_id, e] : entities_) {
        if (e.attached && e.parent_id == id) {
            out.push_back(child_id);
        }
    }
    return out;
}

} // namespace cigi::mock
