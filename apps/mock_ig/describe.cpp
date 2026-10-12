#include "mock_ig/describe.hpp"

#include <variant>

namespace cigi::mock {

namespace {

/// Builds "Name key=value key=value ...".
class Line {
public:
    explicit Line(std::string_view name) : text_(name) {}

    template <class T> Line& kv(std::string_view key, const T& value) {
        text_ += std::format(" {}={}", key, value);
        return *this;
    }
    /// A floating-point value with `digits` decimals and a unit suffix.
    Line& num(std::string_view key, double value, std::string_view unit, int digits = 3) {
        text_ += std::format(" {}={:.{}f}{}", key, value, digits, unit);
        return *this;
    }
    Line& num(std::string_view key, float value, std::string_view unit, int digits = 3) {
        return num(key, static_cast<double>(value), unit, digits);
    }
    Line& flag(std::string_view key, bool value) {
        text_ += std::format(" {}={}", key, value ? "on" : "off");
        return *this;
    }
    template <class E> Line& en(std::string_view key, E value) {
        text_ += std::format(" {}={}", key, enum_name(value));
        return *this;
    }
    [[nodiscard]] std::string str() && { return std::move(text_); }

private:
    std::string text_;
};

/// "x=1.000m y=2.000m z=3.000m" for the offsets whose enable flag is set.
void offsets(Line& l, bool ex, float x, bool ey, float y, bool ez, float z) {
    if (ex) {
        l.num("x", x, "m");
    }
    if (ey) {
        l.num("y", y, "m");
    }
    if (ez) {
        l.num("z", z, "m");
    }
}

void angles(Line& l, bool er, float roll, bool ep, float pitch, bool ey, float yaw) {
    if (er) {
        l.num("roll", roll, "deg");
    }
    if (ep) {
        l.num("pitch", pitch, "deg");
    }
    if (ey) {
        l.num("yaw", yaw, "deg");
    }
}

} // namespace

std::string describe(const IgControl& p) {
    Line l{"IgControl"};
    l.kv("host_frame", p.host_frame_number)
        .en("mode", p.ig_mode)
        .kv("db", static_cast<int>(p.database_number))
        .flag("extrapolation", p.extrapolation_enabled)
        .kv("last_ig_frame", p.last_ig_frame_number);
    if (p.timestamp_valid) {
        l.num("timestamp", static_cast<double>(p.timestamp.count()) * 10.0, "us", 0);
    }
    return std::move(l).str();
}

std::string describe(const StartOfFrame& p) {
    Line l{"StartOfFrame"};
    l.kv("ig_frame", p.ig_frame_number)
        .en("mode", p.ig_mode)
        .kv("db", static_cast<int>(p.database_number))
        .kv("status", static_cast<unsigned>(p.ig_status))
        .en("erm", p.earth_reference_model)
        .kv("last_host_frame", p.last_host_frame_number);
    if (p.timestamp_valid) {
        l.num("timestamp", static_cast<double>(p.timestamp.count()) * 10.0, "us", 0);
    }
    return std::move(l).str();
}

std::string describe(const EntityControl& p) {
    Line l{"EntityControl"};
    l.kv("id", p.entity_id)
        .en("state", p.entity_state)
        .kv("type", p.entity_type)
        .kv("alpha", static_cast<unsigned>(p.alpha))
        .en("attach", p.attach_state);
    if (p.attach_state == AttachState::Attach) {
        l.kv("parent", p.parent_id)
            .flag("inherit_alpha", p.inherit_alpha)
            .num("x", p.latitude_or_x, "m")
            .num("y", p.longitude_or_y, "m")
            .num("z", p.altitude_or_z, "m");
    } else {
        l.en("clamp", p.ground_clamp)
            .num("lat", p.latitude_or_x, "deg", 6)
            .num("lon", p.longitude_or_y, "deg", 6)
            .num("alt", p.altitude_or_z, "m");
    }
    l.num("roll", static_cast<double>(p.roll), "deg")
        .num("pitch", static_cast<double>(p.pitch), "deg")
        .num("yaw", static_cast<double>(p.yaw), "deg")
        .flag("collision", p.collision_detection_enabled)
        .flag("extrapolate", p.extrapolation_enabled);
    if (p.animation_state != AnimationState::Stop) {
        l.en("anim", p.animation_state)
            .en("anim_dir", p.animation_direction)
            .en("anim_loop", p.animation_loop_mode);
    }
    return std::move(l).str();
}

std::string describe(const ComponentControl& p) {
    Line l{"ComponentControl"};
    l.en("class", p.component_class)
        .kv("id", p.component_id)
        .kv("instance", p.instance_id)
        .kv("state", static_cast<unsigned>(p.component_state))
        .kv("data", std::format("{:08x} {:08x} {:08x} {:08x} {:08x} {:08x}", p.data_1, p.data_2,
                                p.data_3, p.data_4, p.data_5, p.data_6));
    return std::move(l).str();
}

std::string describe(const ArticulatedPartControl& p) {
    Line l{"ArticulatedPartControl"};
    l.kv("entity", p.entity_id)
        .kv("part", static_cast<unsigned>(p.articulated_part_id))
        .flag("enabled", p.part_enabled);
    offsets(l, p.x_offset_enabled, p.x_offset, p.y_offset_enabled, p.y_offset, p.z_offset_enabled,
            p.z_offset);
    angles(l, p.roll_enabled, p.roll, p.pitch_enabled, p.pitch, p.yaw_enabled, p.yaw);
    return std::move(l).str();
}

std::string describe(const RateControl& p) {
    Line l{"RateControl"};
    l.kv("entity", p.entity_id);
    if (p.apply_to_articulated_part) {
        l.kv("part", static_cast<unsigned>(p.articulated_part_id));
    } else {
        l.en("frame", p.coordinate_system);
    }
    l.num("vx", static_cast<double>(p.x_linear_rate), "m/s")
        .num("vy", static_cast<double>(p.y_linear_rate), "m/s")
        .num("vz", static_cast<double>(p.z_linear_rate), "m/s")
        .num("roll_rate", static_cast<double>(p.roll_rate), "deg/s")
        .num("pitch_rate", static_cast<double>(p.pitch_rate), "deg/s")
        .num("yaw_rate", static_cast<double>(p.yaw_rate), "deg/s");
    return std::move(l).str();
}

std::string describe(const CelestialSphereControl& p) {
    Line l{"CelestialSphereControl"};
    l.kv("utc", std::format("{:02}:{:02}", p.hour, p.minute));
    if (const auto date = p.calendar_date()) {
        l.kv("date",
             std::format("{:04}-{:02}-{:02}", static_cast<int>(date->year()),
                         static_cast<unsigned>(date->month()), static_cast<unsigned>(date->day())));
    } else {
        l.kv("date", std::format("{}(invalid)", p.date));
    }
    l.flag("time_valid", p.date_time_valid)
        .flag("ephemeris", p.ephemeris_model_enabled)
        .flag("sun", p.sun_enabled)
        .flag("moon", p.moon_enabled)
        .flag("stars", p.star_field_enabled)
        .num("star_intensity", static_cast<double>(p.star_field_intensity), "%", 1);
    return std::move(l).str();
}

std::string describe(const ViewControl& p) {
    Line l{"ViewControl"};
    if (p.group_id != 0) {
        l.kv("group", static_cast<unsigned>(p.group_id));
    } else {
        l.kv("view", p.view_id);
    }
    l.kv("entity", p.entity_id);
    offsets(l, p.x_offset_enabled, p.x_offset, p.y_offset_enabled, p.y_offset, p.z_offset_enabled,
            p.z_offset);
    angles(l, p.roll_enabled, p.roll, p.pitch_enabled, p.pitch, p.yaw_enabled, p.yaw);
    return std::move(l).str();
}

std::string describe(const ViewDefinition& p) {
    Line l{"ViewDefinition"};
    l.kv("view", p.view_id)
        .kv("group", static_cast<unsigned>(p.group_id))
        .en("projection", p.projection_type)
        .en("mirror", p.mirror_mode)
        .en("replication", p.pixel_replication_mode)
        .kv("type", static_cast<unsigned>(p.view_type));
    if (p.near_enabled) {
        l.num("near", static_cast<double>(p.near_plane), "m");
    }
    if (p.far_enabled) {
        l.num("far", static_cast<double>(p.far_plane), "m");
    }
    if (p.left_enabled) {
        l.num("left", static_cast<double>(p.left), "deg");
    }
    if (p.right_enabled) {
        l.num("right", static_cast<double>(p.right), "deg");
    }
    if (p.top_enabled) {
        l.num("top", static_cast<double>(p.top), "deg");
    }
    if (p.bottom_enabled) {
        l.num("bottom", static_cast<double>(p.bottom), "deg");
    }
    return std::move(l).str();
}

std::string describe(const HatHotRequest& p) {
    Line l{"HatHotRequest"};
    l.kv("id", p.hat_hot_id).en("type", p.request_type);
    if (p.update_period == 0) {
        l.kv("period", "one-shot");
    } else {
        l.kv("period", static_cast<unsigned>(p.update_period));
    }
    if (p.coordinate_system == HatHotCoordinateSystem::Geodetic) {
        l.en("frame", p.coordinate_system)
            .num("lat", p.latitude_or_x, "deg", 6)
            .num("lon", p.longitude_or_y, "deg", 6)
            .num("alt", p.altitude_or_z, "m");
    } else {
        l.en("frame", p.coordinate_system)
            .kv("entity", p.entity_id)
            .num("x", p.latitude_or_x, "m")
            .num("y", p.longitude_or_y, "m")
            .num("z", p.altitude_or_z, "m");
    }
    return std::move(l).str();
}

std::string describe(const HatHotResponse& p) {
    Line l{"HatHotResponse"};
    l.kv("id", p.hat_hot_id).flag("valid", p.valid).en("type", p.response_type);
    if (p.valid) {
        l.num("height", p.height, "m");
    }
    l.kv("host_frame_lsn", static_cast<unsigned>(p.host_frame_lsn));
    return std::move(l).str();
}

std::string describe(const HatHotExtendedResponse& p) {
    Line l{"HatHotExtendedResponse"};
    l.kv("id", p.hat_hot_id).flag("valid", p.valid);
    if (p.valid) {
        l.num("hat", p.hat, "m")
            .num("hot", p.hot, "m")
            .kv("material", p.material_code)
            .num("normal_az", static_cast<double>(p.normal_azimuth), "deg")
            .num("normal_el", static_cast<double>(p.normal_elevation), "deg");
    }
    l.kv("host_frame_lsn", static_cast<unsigned>(p.host_frame_lsn));
    return std::move(l).str();
}

std::string describe(const UnknownPacket& p) {
    Line l{"UnknownPacket"};
    l.kv("opcode", static_cast<unsigned>(p.opcode)).kv("size", p.bytes.size());
    return std::move(l).str();
}

std::string describe_any(const AnyPacket<HostToIgPackets>& p) {
    return std::visit([](const auto& packet) { return describe(packet); }, p);
}

std::string describe_any(const AnyPacket<IgToHostPackets>& p) {
    return std::visit([](const auto& packet) { return describe(packet); }, p);
}

} // namespace cigi::mock
