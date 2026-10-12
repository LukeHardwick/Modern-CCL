#include "mock_ig/hat_hot.hpp"

#include <algorithm>
#include <cmath>

namespace cigi::mock {

namespace {

/// The test point of a request in geodetic terms; empty if it cannot be located (an entity
/// frame whose entity does not exist).
std::optional<Geodetic> test_point(const SceneModel& scene, const HatHotRequest& r) {
    if (r.coordinate_system == HatHotCoordinateSystem::Geodetic) {
        return Geodetic{r.latitude_or_x, r.longitude_or_y, r.altitude_or_z};
    }
    const Entity* e = scene.find(r.entity_id);
    if (e == nullptr) {
        return std::nullopt;
    }
    // Entity frame: X, Y, Z offsets from the reference point along the body axes (ICD 4.1.24).
    const Vec3d offset{r.latitude_or_x, r.longitude_or_y, r.altitude_or_z};
    return scene.frame().to_geodetic(e->enu + (e->body_to_enu * offset));
}

} // namespace

HatHotResponder::Reply HatHotResponder::answer(const SceneModel& scene, const HatHotRequest& r,
                                               bool periodic, HatHotProbe& probe) {
    probe.request = r;
    probe.periodic = periodic;
    const auto point = test_point(scene, r);
    probe.valid = point && scene.config().bounds.contains(point->lat_deg, point->lon_deg);
    const std::uint8_t lsn = periodic ? host_frame_lsn(scene.host_frame_number()) : 0;
    if (probe.valid) {
        probe.point = *point;
        probe.hot_m = terrain_height(point->lat_deg, point->lon_deg);
        probe.hat_m = point->alt_m - probe.hot_m;
        probe.material = terrain_material(point->lat_deg, point->lon_deg);
        probe.normal = terrain_normal(point->lat_deg, point->lon_deg);
    } else if (point) {
        probe.point = *point;
    }

    if (r.request_type == HatHotRequestType::Extended) {
        HatHotExtendedResponse out;
        out.hat_hot_id = r.hat_hot_id;
        out.valid = probe.valid;
        out.host_frame_lsn = lsn;
        if (probe.valid) {
            out.hat = probe.hat_m;
            out.hot = probe.hot_m;
            out.material_code = static_cast<std::uint32_t>(probe.material);
            out.normal_azimuth = static_cast<float>(probe.normal.azimuth_deg);
            out.normal_elevation = static_cast<float>(probe.normal.elevation_deg);
        }
        return out;
    }
    HatHotResponse out;
    out.hat_hot_id = r.hat_hot_id;
    out.valid = probe.valid;
    out.host_frame_lsn = lsn;
    // Request Type 0 is HAT, anything else asks for HOT; Extended was handled above.
    const bool want_hot = r.request_type == HatHotRequestType::Hot;
    out.response_type = want_hot ? HatHotResponseType::Hot : HatHotResponseType::Hat;
    if (probe.valid) {
        out.height = want_hot ? probe.hot_m : probe.hat_m;
    }
    return out;
}

void HatHotResponder::submit(const HatHotRequest& request) {
    if (request.update_period == 0) {
        // One-shot. Re-using the ID of a periodic request cancels that one (ICD 4.1.24).
        periodic_.erase(request.hat_hot_id);
        pending_.push_back(request);
        return;
    }
    periodic_.insert_or_assign(request.hat_hot_id, Periodic{request, 0});
}

std::vector<HatHotResponder::Reply> HatHotResponder::collect(SceneModel& scene,
                                                             std::uint64_t ig_frame) {
    scene.resolve();
    std::vector<Reply> out;
    const auto emit = [&](const HatHotRequest& r, bool periodic) {
        HatHotProbe probe;
        probe.ig_frame = ig_frame;
        out.push_back(answer(scene, r, periodic, probe));
        scene.add_probe(probe);
    };
    for (const HatHotRequest& r : pending_) {
        emit(r, false);
    }
    pending_.clear();

    for (auto it = periodic_.begin(); it != periodic_.end();) {
        Periodic& p = it->second;
        if (p.request.coordinate_system == HatHotCoordinateSystem::Entity &&
            scene.find(p.request.entity_id) == nullptr) {
            // "until the Entity is destroyed" (ICD 4.1.24).
            it = periodic_.erase(it);
            continue;
        }
        if (p.frames_until_due == 0) {
            emit(p.request, true);
            p.frames_until_due = static_cast<std::uint8_t>(p.request.update_period - 1);
        } else {
            --p.frames_until_due;
        }
        ++it;
    }
    return out;
}

} // namespace cigi::mock
