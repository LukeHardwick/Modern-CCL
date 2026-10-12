// Writes the Wireshark verification corpus: a pcap holding every MVP packet in distinctive
// configurations (native and foreign byte order, both directions) and a JSON file listing the
// values that were encoded. check_tshark.py decodes the pcap with tshark and compares.
//
// Usage: make_wireshark_corpus <output-dir>     (writes corpus.pcap and expected.json)
//
// Host messages travel 10.0.0.1:8005 -> 10.0.0.2:8004, IG messages the reverse.
#include "cigi/packets.hpp"
#include "cigi/tools/pcap_writer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace cigi;
using tools::Endpoint;
using tools::Ipv4Address;

constexpr Endpoint host_endpoint{.address = Ipv4Address{{10, 0, 0, 1}}, .port = 8005};
constexpr Endpoint ig_endpoint{.address = Ipv4Address{{10, 0, 0, 2}}, .port = 8004};

// --- Expected-value recording -------------------------------------------------------------

using Value = std::variant<std::int64_t, std::uint64_t, double, bool>;

template <class T> Value to_value(const T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        return v;
    } else if constexpr (std::is_enum_v<T>) {
        return static_cast<std::uint64_t>(std::to_underlying(v));
    } else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<double>(v);
    } else if constexpr (std::is_signed_v<T>) {
        return static_cast<std::int64_t>(v);
    } else {
        return static_cast<std::uint64_t>(v);
    }
}

struct Described {
    std::string type;
    std::vector<std::pair<std::string, Value>> fields;

    template <class T> Described& add(std::string name, const T& v) {
        fields.emplace_back(std::move(name), to_value(v));
        return *this;
    }
};

Described describe(const IgControl& p) {
    Described d{.type = "ig_control", .fields = {}};
    d.add("minor_version", p.version.minor)
        .add("database_number", p.database_number)
        .add("ig_mode", p.ig_mode)
        .add("timestamp_valid", p.timestamp_valid)
        .add("extrapolation_enabled", p.extrapolation_enabled)
        .add("host_frame_number", p.host_frame_number)
        .add("timestamp", p.timestamp.count())
        .add("last_ig_frame_number", p.last_ig_frame_number);
    return d;
}

Described describe(const StartOfFrame& p) {
    Described d{.type = "start_of_frame", .fields = {}};
    d.add("minor_version", p.version.minor)
        .add("database_number", p.database_number)
        .add("ig_status", p.ig_status)
        .add("ig_mode", p.ig_mode)
        .add("timestamp_valid", p.timestamp_valid)
        .add("earth_reference_model", p.earth_reference_model)
        .add("ig_frame_number", p.ig_frame_number)
        .add("timestamp", p.timestamp.count())
        .add("last_host_frame_number", p.last_host_frame_number);
    return d;
}

Described describe(const EntityControl& p) {
    Described d{.type = "entity_control", .fields = {}};
    d.add("entity_id", p.entity_id)
        .add("entity_state", p.entity_state)
        .add("attach_state", p.attach_state)
        .add("collision_detection_enabled", p.collision_detection_enabled)
        .add("inherit_alpha", p.inherit_alpha)
        .add("ground_clamp", p.ground_clamp)
        .add("animation_direction", p.animation_direction)
        .add("animation_loop_mode", p.animation_loop_mode)
        .add("animation_state", p.animation_state)
        .add("extrapolation_enabled", p.extrapolation_enabled)
        .add("alpha", p.alpha)
        .add("entity_type", p.entity_type)
        .add("parent_id", p.parent_id)
        .add("roll", p.roll)
        .add("pitch", p.pitch)
        .add("yaw", p.yaw)
        .add("latitude_or_x", p.latitude_or_x)
        .add("longitude_or_y", p.longitude_or_y)
        .add("altitude_or_z", p.altitude_or_z);
    return d;
}

Described describe(const ArticulatedPartControl& p) {
    Described d{.type = "articulated_part_control", .fields = {}};
    d.add("entity_id", p.entity_id)
        .add("articulated_part_id", p.articulated_part_id)
        .add("part_enabled", p.part_enabled)
        .add("x_offset_enabled", p.x_offset_enabled)
        .add("y_offset_enabled", p.y_offset_enabled)
        .add("z_offset_enabled", p.z_offset_enabled)
        .add("roll_enabled", p.roll_enabled)
        .add("pitch_enabled", p.pitch_enabled)
        .add("yaw_enabled", p.yaw_enabled)
        .add("x_offset", p.x_offset)
        .add("y_offset", p.y_offset)
        .add("z_offset", p.z_offset)
        .add("roll", p.roll)
        .add("pitch", p.pitch)
        .add("yaw", p.yaw);
    return d;
}

Described describe(const RateControl& p) {
    Described d{.type = "rate_control", .fields = {}};
    d.add("entity_id", p.entity_id)
        .add("articulated_part_id", p.articulated_part_id)
        .add("apply_to_articulated_part", p.apply_to_articulated_part)
        .add("coordinate_system", p.coordinate_system)
        .add("x_linear_rate", p.x_linear_rate)
        .add("y_linear_rate", p.y_linear_rate)
        .add("z_linear_rate", p.z_linear_rate)
        .add("roll_rate", p.roll_rate)
        .add("pitch_rate", p.pitch_rate)
        .add("yaw_rate", p.yaw_rate);
    return d;
}

Described describe(const ComponentControl& p) {
    Described d{.type = "component_control", .fields = {}};
    d.add("component_id", p.component_id)
        .add("instance_id", p.instance_id)
        .add("component_class", p.component_class)
        .add("component_state", p.component_state)
        .add("data_1", p.data_1)
        .add("data_2", p.data_2)
        .add("data_3", p.data_3)
        .add("data_4", p.data_4)
        .add("data_5", p.data_5)
        .add("data_6", p.data_6);
    return d;
}

Described describe(const CelestialSphereControl& p) {
    Described d{.type = "celestial_sphere_control", .fields = {}};
    d.add("hour", p.hour)
        .add("minute", p.minute)
        .add("ephemeris_model_enabled", p.ephemeris_model_enabled)
        .add("sun_enabled", p.sun_enabled)
        .add("moon_enabled", p.moon_enabled)
        .add("star_field_enabled", p.star_field_enabled)
        .add("date_time_valid", p.date_time_valid)
        .add("date", p.date)
        .add("star_field_intensity", p.star_field_intensity);
    return d;
}

Described describe(const ViewControl& p) {
    Described d{.type = "view_control", .fields = {}};
    d.add("view_id", p.view_id)
        .add("group_id", p.group_id)
        .add("x_offset_enabled", p.x_offset_enabled)
        .add("y_offset_enabled", p.y_offset_enabled)
        .add("z_offset_enabled", p.z_offset_enabled)
        .add("roll_enabled", p.roll_enabled)
        .add("pitch_enabled", p.pitch_enabled)
        .add("yaw_enabled", p.yaw_enabled)
        .add("entity_id", p.entity_id)
        .add("x_offset", p.x_offset)
        .add("y_offset", p.y_offset)
        .add("z_offset", p.z_offset)
        .add("roll", p.roll)
        .add("pitch", p.pitch)
        .add("yaw", p.yaw);
    return d;
}

Described describe(const ViewDefinition& p) {
    Described d{.type = "view_definition", .fields = {}};
    d.add("view_id", p.view_id)
        .add("group_id", p.group_id)
        .add("near_enabled", p.near_enabled)
        .add("far_enabled", p.far_enabled)
        .add("left_enabled", p.left_enabled)
        .add("right_enabled", p.right_enabled)
        .add("top_enabled", p.top_enabled)
        .add("bottom_enabled", p.bottom_enabled)
        .add("mirror_mode", p.mirror_mode)
        .add("pixel_replication_mode", p.pixel_replication_mode)
        .add("projection_type", p.projection_type)
        .add("reorder", p.reorder)
        .add("view_type", p.view_type)
        .add("near_plane", p.near_plane)
        .add("far_plane", p.far_plane)
        .add("left", p.left)
        .add("right", p.right)
        .add("top", p.top)
        .add("bottom", p.bottom);
    return d;
}

Described describe(const HatHotRequest& p) {
    Described d{.type = "hat_hot_request", .fields = {}};
    d.add("hat_hot_id", p.hat_hot_id)
        .add("request_type", p.request_type)
        .add("coordinate_system", p.coordinate_system)
        .add("update_period", p.update_period)
        .add("entity_id", p.entity_id)
        .add("latitude_or_x", p.latitude_or_x)
        .add("longitude_or_y", p.longitude_or_y)
        .add("altitude_or_z", p.altitude_or_z);
    return d;
}

Described describe(const HatHotResponse& p) {
    Described d{.type = "hat_hot_response", .fields = {}};
    d.add("hat_hot_id", p.hat_hot_id)
        .add("valid", p.valid)
        .add("response_type", p.response_type)
        .add("host_frame_lsn", p.host_frame_lsn)
        .add("height", p.height);
    return d;
}

Described describe(const HatHotExtendedResponse& p) {
    Described d{.type = "hat_hot_extended_response", .fields = {}};
    d.add("hat_hot_id", p.hat_hot_id)
        .add("valid", p.valid)
        .add("host_frame_lsn", p.host_frame_lsn)
        .add("hat", p.hat)
        .add("hot", p.hot)
        .add("material_code", p.material_code)
        .add("normal_azimuth", p.normal_azimuth)
        .add("normal_elevation", p.normal_elevation);
    return d;
}

// --- Message assembly ---------------------------------------------------------------------

enum class Order : std::uint8_t { Native, Foreign };

using Bytes = std::vector<std::byte>;

/// Reverse every multi-byte field of an encoded packet, as a peer of the opposite endianness would
/// have written it (ICD 2.5). Bit fields and single bytes are order-independent. The field table
/// supplies each scalar's offset and width. IG Control / Start of Frame also swap the magic number.
template <class P> void make_foreign(std::span<std::byte> b) {
    std::apply(
        [&](const auto&... f) {
            const auto swap_one = [&](const auto& field) {
                if (field.size() > 1) {
                    const auto first = b.begin() + static_cast<std::ptrdiff_t>(field.offset);
                    std::reverse(first, first + static_cast<std::ptrdiff_t>(field.size()));
                }
            };
            (swap_one(f), ...);
        },
        P::fields);
    if constexpr (std::is_same_v<P, IgControl> || std::is_same_v<P, StartOfFrame>) {
        std::reverse(b.begin() + 6, b.begin() + 8);
    }
}

struct Message {
    Direction direction;
    Order order;
    bool priming = false; ///< Sent only so that Wireshark learns the CIGI version.
    Bytes bytes;
    std::vector<Described> packets;
};

class Corpus {
public:
    /// A Host message: IG Control first, then `rest`.
    template <class... Ps> void host(Order order, const IgControl& first, const Ps&... rest) {
        add(Direction::HostToIg, order, first, rest...);
    }
    /// An IG message: Start of Frame first, then `rest`.
    template <class... Ps> void ig(Order order, const StartOfFrame& first, const Ps&... rest) {
        add(Direction::IgToHost, order, first, rest...);
    }

    /// Mark the messages added from now on as priming (true) or normal (false).
    void set_priming(bool on) { priming_ = on; }

    [[nodiscard]] const std::vector<Message>& messages() const { return messages_; }

private:
    template <class... Ps> void add(Direction dir, Order order, const Ps&... ps) {
        Message m{
            .direction = dir,
            .order = order,
            .priming = priming_,
            .bytes = {},
            .packets = {},
        };
        (append(m, ps), ...);
        messages_.push_back(std::move(m));
    }

    template <std::ranges::input_range R> static void append(Message& m, const R& packets) {
        for (const auto& p : packets) {
            append(m, p);
        }
    }

    template <class P> static void append(Message& m, const P& p) {
        static_assert(P::direction == Direction::HostToIg || P::direction == Direction::IgToHost);
        if (P::direction != m.direction) {
            std::println(stderr, "packet opcode {} has the wrong direction", int{P::opcode});
            std::exit(EXIT_FAILURE);
        }
        if (!validate(p)) {
            std::println(stderr, "packet opcode {} fails validate()", int{P::opcode});
            std::exit(EXIT_FAILURE);
        }
        Bytes one(encoded_size(p));
        encode(p, one);
        if (m.order == Order::Foreign) {
            make_foreign<P>(one);
        }
        m.bytes.insert(m.bytes.end(), one.begin(), one.end());
        m.packets.push_back(describe(p));
    }

    std::vector<Message> messages_;
    bool priming_ = false;
};

// --- Output -------------------------------------------------------------------------------

std::string json_value(const Value& v) {
    return std::visit(
        [](const auto& x) -> std::string {
            if constexpr (std::is_same_v<std::decay_t<decltype(x)>, bool>) {
                return x ? "true" : "false";
            } else {
                return std::format("{}", x);
            }
        },
        v);
}

std::string to_json(const std::vector<Message>& messages) {
    // The wire byte order of a "native" message is this machine's; "foreign" is the opposite.
    const bool native_little = std::endian::native == std::endian::little;
    std::string out = "{\n  \"messages\": [\n";
    for (std::size_t i = 0; i < messages.size(); ++i) {
        const Message& m = messages.at(i);
        const bool little = (m.order == Order::Native) == native_little;
        out += std::format("    {{\"frame\": {}, \"direction\": \"{}\", \"wire_endian\": \"{}\", "
                           "\"priming\": {},\n",
                           i + 1, m.direction == Direction::HostToIg ? "host_to_ig" : "ig_to_host",
                           little ? "little" : "big", m.priming ? "true" : "false");
        out += "     \"packets\": [\n";
        for (std::size_t j = 0; j < m.packets.size(); ++j) {
            const Described& d = m.packets.at(j);
            out += std::format(R"(       {{"type": "{}", "fields": {{)", d.type);
            for (std::size_t k = 0; k < d.fields.size(); ++k) {
                out += std::format("{}\"{}\": {}", k == 0 ? "" : ", ", d.fields.at(k).first,
                                   json_value(d.fields.at(k).second));
            }
            out += std::format("}}}}{}\n", j + 1 < m.packets.size() ? "," : "");
        }
        out += std::format("     ]}}{}\n", i + 1 < messages.size() ? "," : "");
    }
    out += "  ]\n}\n";
    return out;
}

// --- The corpus ---------------------------------------------------------------------------

using std::chrono::microseconds;

IgControl ig_control(std::uint32_t host_frame, IgMode mode, std::int8_t db, bool tsv, bool extrap,
                     std::uint32_t ticks, std::uint32_t last_ig) {
    return IgControl{
        .database_number = db,
        .ig_mode = mode,
        .timestamp_valid = tsv,
        .extrapolation_enabled = extrap,
        .host_frame_number = host_frame,
        .timestamp = Timestamp{ticks},
        .last_ig_frame_number = last_ig,
        .version = protocol_version,
    };
}

StartOfFrame start_of_frame(std::uint32_t ig_frame, IgMode mode, std::int8_t db,
                            std::uint8_t status, bool tsv, EarthReferenceModel erm,
                            std::uint32_t ticks, std::uint32_t last_host) {
    return StartOfFrame{
        .database_number = db,
        .ig_status = status,
        .ig_mode = mode,
        .timestamp_valid = tsv,
        .earth_reference_model = erm,
        .ig_frame_number = ig_frame,
        .timestamp = Timestamp{ticks},
        .last_host_frame_number = last_host,
        .version = protocol_version,
    };
}

// NOLINTBEGIN(readability-function-size): a data table.
void add_frame_packets(Corpus& c) {
    // With "From Packet", Wireshark shows the first datagram of a capture as Unknown and only then
    // knows the CIGI version (docs/interop/discrepancies.md), so the capture starts with a
    // priming Host message that the checker does not compare.
    c.set_priming(true);
    c.host(Order::Native, ig_control(0, IgMode::ResetStandby, 0, false, false, 0, 0));
    c.set_priming(false);
    c.host(Order::Native, ig_control(1, IgMode::ResetStandby, 0, false, false, 0, 0));
    c.host(Order::Native, ig_control(2, IgMode::Operate, 127, true, true, 1666, 1));
    c.host(Order::Native,
           ig_control(0xFFFFFFFFU, IgMode::Debug, 1, true, false, 0xFFFFFFFFU, 0xFFFFFFFEU));
    c.host(Order::Foreign,
           ig_control(0x01020304U, IgMode::Operate, 100, true, true, 0x0A0B0C0DU, 0xA1B2C3D4U));

    c.ig(Order::Native,
         start_of_frame(1, IgMode::ResetStandby, -128, 0, false, EarthReferenceModel::Wgs84, 0, 0));
    c.ig(Order::Native, start_of_frame(2, IgMode::Operate, -1, 255, true,
                                       EarthReferenceModel::HostDefined, 1666, 1));
    c.ig(Order::Native, start_of_frame(0xFFFFFFFFU, IgMode::Debug, 127, 7, true,
                                       EarthReferenceModel::Wgs84, 0xFFFFFFFFU, 0xFFFFFFFEU));
    c.ig(Order::Native, start_of_frame(4, IgMode::OfflineMaintenance, 0, 128, false,
                                       EarthReferenceModel::HostDefined, 12345, 3));
    c.ig(Order::Foreign,
         start_of_frame(0x11223344U, IgMode::Operate, -77, 0x5A, true,
                        EarthReferenceModel::HostDefined, 0x55667788U, 0x99AABBCCU));
}

EntityControl entity(std::uint16_t id) {
    EntityControl e;
    e.entity_id = id;
    return e;
}

void add_entity_packets(Corpus& c) {
    const auto frame = [](std::uint32_t n) {
        return ig_control(n, IgMode::Operate, 1, true, false, n * 1667, n - 1);
    };
    EntityControl a = entity(0);
    a.entity_state = EntityState::Active;
    a.collision_detection_enabled = true;
    a.alpha = 255;
    a.entity_type = 1234;
    a.roll = -180.0F;
    a.pitch = -90.0F;
    a.yaw = 0.0F;
    a.latitude_or_x = -90.0;
    a.longitude_or_y = -180.0;
    a.altitude_or_z = -1000.5;

    EntityControl b = entity(0xFFFF);
    b.entity_state = EntityState::Destroyed;
    b.attach_state = AttachState::Attach;
    b.inherit_alpha = true;
    b.ground_clamp = GroundClamp::NonConformal;
    b.animation_direction = AnimationDirection::Backward;
    b.animation_loop_mode = AnimationLoopMode::Continuous;
    b.animation_state = AnimationState::Play;
    b.extrapolation_enabled = true;
    b.alpha = 0;
    b.entity_type = 0xFFFF;
    b.parent_id = 0xFFFE;
    b.roll = 179.5F;
    b.pitch = 89.25F;
    b.yaw = 360.0F;
    b.latitude_or_x = 12345.678;
    b.longitude_or_y = -9876.54321;
    b.altitude_or_z = 0.125;

    EntityControl d = entity(42);
    d.entity_state = EntityState::InactiveStandby;
    d.ground_clamp = GroundClamp::Conformal;
    d.animation_state = AnimationState::Pause;
    d.alpha = 128;
    d.entity_type = 7;
    d.roll = 0.5F;
    d.pitch = -0.25F;
    d.yaw = 359.5F;
    d.latitude_or_x = 51.4775;
    d.longitude_or_y = -0.0005;
    d.altitude_or_z = 45.25;

    EntityControl e = entity(1);
    e.animation_state = AnimationState::Continue;
    e.ground_clamp = GroundClamp::NoClamp;
    e.entity_state = EntityState::Active;
    e.attach_state = AttachState::Detach;
    e.collision_detection_enabled = true;
    e.yaw = 90.0F;
    e.latitude_or_x = 90.0;
    e.longitude_or_y = 180.0;
    e.altitude_or_z = 8848.86;

    c.host(Order::Native, frame(10), a, b);
    c.host(Order::Native, frame(11), d, e);
    c.host(Order::Foreign, frame(12), a, b, d, e);

    ArticulatedPartControl p;
    p.entity_id = 0x1234;
    p.articulated_part_id = 255;
    p.part_enabled = true;
    p.x_offset_enabled = p.y_offset_enabled = p.z_offset_enabled = true;
    p.roll_enabled = p.pitch_enabled = p.yaw_enabled = true;
    p.x_offset = 1.5F;
    p.y_offset = -2.25F;
    p.z_offset = 1000.0F;
    p.roll = -180.0F;
    p.pitch = 90.0F;
    p.yaw = 360.0F;
    ArticulatedPartControl q;
    q.entity_id = 0;
    q.articulated_part_id = 1;
    q.yaw_enabled = true;
    q.yaw = 45.5F;
    q.x_offset = -0.5F;
    ArticulatedPartControl r;
    r.entity_id = 0xFFFF;
    r.articulated_part_id = 0x80;
    r.part_enabled = true;
    r.z_offset_enabled = true;
    r.pitch_enabled = true;
    r.z_offset = -12.75F;
    r.pitch = -89.0F;
    c.host(Order::Native, frame(13), p, q, r);
    c.host(Order::Foreign, frame(14), p, q, r);

    RateControl ra;
    ra.entity_id = 0xABCD;
    ra.articulated_part_id = 17;
    ra.apply_to_articulated_part = true;
    ra.coordinate_system = RateCoordinateSystem::Local;
    ra.x_linear_rate = 10.5F;
    ra.y_linear_rate = -20.25F;
    ra.z_linear_rate = 0.0625F;
    ra.roll_rate = -5.5F;
    ra.pitch_rate = 3.75F;
    ra.yaw_rate = 720.0F;
    RateControl rb;
    rb.entity_id = 3;
    rb.x_linear_rate = -300.0F;
    rb.yaw_rate = -0.5F;
    c.host(Order::Native, frame(15), ra, rb);
    c.host(Order::Foreign, frame(16), ra, rb);
}

void add_component_packets(Corpus& c) {
    const auto frame = [](std::uint32_t n) {
        return ig_control(n, IgMode::Operate, 1, true, false, n * 1667, n - 1);
    };
    // One packet per Component Class (ICD 4.1.4, Table 10): 0..15.
    std::vector<ComponentControl> all;
    for (std::uint8_t k = 0; k <= 15; ++k) {
        ComponentControl p;
        p.component_id = static_cast<std::uint16_t>(0x100U + k);
        p.instance_id = static_cast<std::uint16_t>(0xF000U - k);
        p.component_class = static_cast<ComponentClass>(k);
        p.component_state = static_cast<std::uint8_t>(0xF0U | k);
        p.data_1 = 0x01020304U * (k + 1U);
        p.data_2 = 0xA0000000U + k;
        p.data_3 = 0x0000FF00U + k;
        p.data_4 = 0x80000000U >> k;
        p.data_5 = 0xFFFFFFFFU - k;
        p.data_6 = k;
        all.push_back(p);
    }
    // Typed data: a 64-bit double (the ICD's own example, m = 100.0, Fig. 46), a float, two u16s,
    // four u8s and a negative i32.
    ComponentControl typed;
    typed.component_id = 0x0ABC;
    typed.instance_id = 0x0DEF;
    typed.component_class = ComponentClass::Event;
    typed.component_state = 255;
    ComponentData data;
    data.set_f64(0, 100.0);
    data.set_f32(2, -1.5F);
    data.set_u16x2(3, 0x1234, 0x5678);
    data.set_u8x4(4, 0x9A, 0xBC, 0xDE, 0xF0);
    data.set_i32(5, -2'000'000'000);
    typed.set_data(data);

    ComponentControl wide;
    wide.component_id = 0xFFFF;
    wide.instance_id = 0xFFFF;
    wide.component_class = ComponentClass::Symbol;
    wide.component_state = 0;
    ComponentData wide_data;
    wide_data.set_u64(0, 0x0102030405060708ULL);
    wide_data.set_i64(2, -2);
    wide_data.set_f64(4, 6.02214076e23);
    wide.set_data(wide_data);

    c.host(Order::Native, frame(20), std::span{all}.first(8));
    c.host(Order::Native, frame(21), std::span{all}.last(8));
    c.host(Order::Native, frame(22), typed, wide);
    c.host(Order::Foreign, frame(23), all.at(0), all.at(7), all.at(15), typed, wide);
}

void add_view_sky_packets(Corpus& c) {
    const auto frame = [](std::uint32_t n) {
        return ig_control(n, IgMode::Operate, 1, true, false, n * 1667, n - 1);
    };
    CelestialSphereControl s1;
    s1.hour = 23;
    s1.minute = 59;
    s1.ephemeris_model_enabled = true;
    s1.sun_enabled = true;
    s1.moon_enabled = true;
    s1.star_field_enabled = true;
    s1.date_time_valid = true;
    s1.set_date(std::chrono::year{2026} / std::chrono::October / std::chrono::day{8});
    s1.star_field_intensity = 100.0F;
    CelestialSphereControl s2;
    s2.hour = 0;
    s2.minute = 0;
    s2.ephemeris_model_enabled = false;
    s2.moon_enabled = true;
    s2.date_time_valid = false;
    s2.set_date(std::chrono::year{1999} / std::chrono::December / std::chrono::day{31});
    s2.star_field_intensity = 12.5F;
    CelestialSphereControl s3;
    s3.hour = 12;
    s3.minute = 30;
    s3.sun_enabled = true;
    s3.set_date(std::chrono::year{2000} / std::chrono::January / std::chrono::day{1});
    s3.star_field_intensity = 0.0F;
    c.host(Order::Native, frame(30), s1, s2, s3);
    c.host(Order::Foreign, frame(31), s1, s2, s3);

    ViewControl v1;
    v1.view_id = 0xFFFF;
    v1.group_id = 255;
    v1.x_offset_enabled = v1.y_offset_enabled = v1.z_offset_enabled = true;
    v1.roll_enabled = v1.pitch_enabled = v1.yaw_enabled = true;
    v1.entity_id = 0xFEDC;
    v1.x_offset = 1.5F;
    v1.y_offset = -2.5F;
    v1.z_offset = 3.25F;
    v1.roll = -180.0F;
    v1.pitch = 90.0F;
    v1.yaw = 360.0F;
    ViewControl v2;
    v2.view_id = 1;
    v2.group_id = 0;
    v2.pitch_enabled = true;
    v2.entity_id = 0;
    v2.pitch = -45.5F;
    ViewControl v3;
    v3.view_id = 0x0102;
    v3.group_id = 9;
    v3.z_offset_enabled = true;
    v3.yaw_enabled = true;
    v3.entity_id = 77;
    v3.z_offset = -100.0F;
    v3.yaw = 270.0F;
    c.host(Order::Native, frame(32), v1, v2, v3);
    c.host(Order::Foreign, frame(33), v1, v2, v3);

    // View Definition: every Mirror Mode (0..3) in the first message, every Pixel Replication
    // value (0..7) and View Type (0..7) in the second.
    std::vector<ViewDefinition> mirrors;
    for (std::uint8_t m = 0; m <= 3; ++m) {
        ViewDefinition d;
        d.view_id = static_cast<std::uint16_t>(0x0100U + m);
        d.group_id = m;
        d.mirror_mode = static_cast<MirrorMode>(m);
        d.projection_type =
            m % 2 == 0 ? ProjectionType::Perspective : ProjectionType::OrthographicParallel;
        d.reorder = m >= 2;
        d.near_enabled = d.far_enabled = true;
        d.near_plane = 0.5F + static_cast<float>(m);
        d.far_plane = 100000.0F;
        mirrors.push_back(d);
    }
    std::vector<ViewDefinition> repl;
    for (std::uint8_t m = 0; m <= 7; ++m) {
        ViewDefinition d;
        d.view_id = static_cast<std::uint16_t>(0xFF00U + m);
        d.group_id = static_cast<std::uint8_t>(255 - m);
        d.pixel_replication_mode = static_cast<PixelReplicationMode>(m);
        d.view_type = m;
        d.left_enabled = d.right_enabled = d.top_enabled = d.bottom_enabled = true;
        d.left = -45.0F + static_cast<float>(m);
        d.right = 44.5F;
        d.top = 30.25F;
        d.bottom = -30.75F - static_cast<float>(m);
        repl.push_back(d);
    }
    c.host(Order::Native, frame(34), mirrors);
    c.host(Order::Native, frame(35), repl);
    c.host(Order::Foreign, frame(36), mirrors.at(3), repl.at(7), repl.at(1));
}

void add_hat_hot_packets(Corpus& c) {
    const auto frame = [](std::uint32_t n) {
        return ig_control(n, IgMode::Operate, 1, true, false, n * 1667, n - 1);
    };
    std::vector<HatHotRequest> reqs;
    for (std::uint8_t t = 0; t <= 2; ++t) {
        for (std::uint8_t cs = 0; cs <= 1; ++cs) {
            HatHotRequest r;
            r.hat_hot_id = static_cast<std::uint16_t>(0x1000U + (t * 2U) + cs);
            r.request_type = static_cast<HatHotRequestType>(t);
            r.coordinate_system = static_cast<HatHotCoordinateSystem>(cs);
            r.update_period = static_cast<std::uint8_t>((t * 100U) + cs);
            r.entity_id = cs == 1 ? std::uint16_t{0xBEEF} : std::uint16_t{0};
            r.latitude_or_x = cs == 1 ? 123.456 : 89.999999;
            r.longitude_or_y = cs == 1 ? -654.321 : -179.999999;
            r.altitude_or_z = cs == 1 ? -0.001 : 12000.75;
            reqs.push_back(r);
        }
    }
    c.host(Order::Native, frame(40), std::span{reqs}.first(3));
    c.host(Order::Native, frame(41), std::span{reqs}.last(3));
    c.host(Order::Foreign, frame(42), reqs);

    const auto sof = [](std::uint32_t n) {
        return start_of_frame(n, IgMode::Operate, 1, 0, true, EarthReferenceModel::Wgs84, n * 1667,
                              n - 1);
    };
    const HatHotResponse h1{
        .hat_hot_id = 0x1000,
        .valid = true,
        .response_type = HatHotResponseType::Hat,
        .host_frame_lsn = 15,
        .height = 123.456,
    };
    const HatHotResponse h2{
        .hat_hot_id = 0xFFFF,
        .valid = false,
        .response_type = HatHotResponseType::Hot,
        .host_frame_lsn = 0,
        .height = -9876.5,
    };
    const HatHotResponse h3{
        .hat_hot_id = 7,
        .valid = true,
        .response_type = HatHotResponseType::Hot,
        .host_frame_lsn = 9,
        .height = -0.001,
    };
    const HatHotExtendedResponse x1{
        .hat_hot_id = 0x2000,
        .valid = true,
        .host_frame_lsn = 10,
        .hat = 55.5,
        .hot = -3.125,
        .material_code = 0xFFFFFFFFU,
        .normal_azimuth = -179.5F,
        .normal_elevation = 89.5F,
    };
    const HatHotExtendedResponse x2{
        .hat_hot_id = 0xFFFE,
        .valid = false,
        .host_frame_lsn = 1,
        .hat = -1000.25,
        .hot = 8848.86,
        .material_code = 0x80000001U,
        .normal_azimuth = 180.0F,
        .normal_elevation = -90.0F,
    };
    c.ig(Order::Native, sof(50), h1, h2, h3);
    c.ig(Order::Native, sof(51), x1, x2);
    c.ig(Order::Foreign, sof(52), h1, h2, h3, x1, x2);
}
// NOLINTEND(readability-function-size)

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape): std::print may throw; a tool may terminate.
int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 2) {
        std::println(stderr, "usage: {} <output-dir>", args.front());
        return EXIT_FAILURE;
    }
    const std::filesystem::path dir{args.back()};
    std::filesystem::create_directories(dir);

    Corpus corpus;
    add_frame_packets(corpus);
    add_entity_packets(corpus);
    add_component_packets(corpus);
    add_view_sky_packets(corpus);
    add_hat_hot_packets(corpus);

    tools::PcapWriter pcap{dir / "corpus.pcap"};
    microseconds t{1'700'000'000LL * 1'000'000};
    for (const Message& m : corpus.messages()) {
        const bool to_ig = m.direction == Direction::HostToIg;
        const auto written = pcap.write_udp(t, to_ig ? host_endpoint : ig_endpoint,
                                            to_ig ? ig_endpoint : host_endpoint, m.bytes);
        if (!written) {
            std::println(stderr, "cannot write {}", (dir / "corpus.pcap").string());
            return EXIT_FAILURE;
        }
        t += microseconds{1000};
    }
    pcap.flush();

    std::ofstream json(dir / "expected.json");
    json << to_json(corpus.messages());
    if (!pcap.ok() || !json) {
        std::println(stderr, "cannot write into {}", dir.string());
        return EXIT_FAILURE;
    }
    std::println("wrote {} datagrams to {}", pcap.frames(), (dir / "corpus.pcap").string());
    return EXIT_SUCCESS;
}
