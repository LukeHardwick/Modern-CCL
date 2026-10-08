#pragma once

// Component Control packet (ICD 4.1.4), opcode 4, Host -> IG, 32 bytes. Table-driven.
//
//   2-3 component id   4-5 instance id
//   6 bits 7-6 reserved, 5-0 component class   7 component state
//   8-31 six 32-bit user-defined words (each byte-swapped as a whole u32, ICD 2.7.1)
//
// Use ComponentData (component_data.hpp) to put 8/16/64-bit and floating-point values into the
// words without corrupting them when the receiver swaps bytes.

#include "cigi/packet.hpp"
#include "cigi/packets/component_data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

namespace cigi {

/// Component Class (ICD 4.1.4, Table 10; 14 and 15 are new in 3.3). 16..63 are reserved.
enum class ComponentClass : std::uint8_t {
    Entity = 0,
    View = 1,
    ViewGroup = 2,
    Sensor = 3,
    RegionalSeaSurface = 4,
    RegionalTerrainSurface = 5,
    RegionalLayeredWeather = 6,
    GlobalSeaSurface = 7,
    GlobalTerrainSurface = 8,
    GlobalLayeredWeather = 9,
    Atmosphere = 10,
    CelestialSphere = 11,
    Event = 12,
    System = 13,
    SymbolSurface = 14,
    Symbol = 15,
};

template <> struct EnumTraits<ComponentClass> {
    [[nodiscard]] static constexpr bool is_defined(ComponentClass v) noexcept {
        return enum_between(v, ComponentClass::Entity, ComponentClass::Symbol);
    }
};

struct ComponentControl {
    static constexpr std::uint8_t opcode = 4;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 32;

    std::uint16_t component_id = 0; ///< For Regional Layered Weather the MSB is the layer ID.
    std::uint16_t instance_id = 0;  ///< Meaning depends on component_class (ICD Table 9).
    ComponentClass component_class = ComponentClass::Entity;
    std::uint8_t component_state = 0;
    std::uint32_t data_1 = 0; ///< User-defined words; see data() / set_data().
    std::uint32_t data_2 = 0;
    std::uint32_t data_3 = 0;
    std::uint32_t data_4 = 0;
    std::uint32_t data_5 = 0;
    std::uint32_t data_6 = 0;

    static constexpr auto fields = std::tuple{
        wire::scalar(&ComponentControl::component_id, 2),
        wire::scalar(&ComponentControl::instance_id, 4),
        wire::bits(&ComponentControl::component_class, 6, 0, 6),
        wire::scalar(&ComponentControl::component_state, 7),
        wire::scalar(&ComponentControl::data_1, 8),
        wire::scalar(&ComponentControl::data_2, 12),
        wire::scalar(&ComponentControl::data_3, 16),
        wire::scalar(&ComponentControl::data_4, 20),
        wire::scalar(&ComponentControl::data_5, 24),
        wire::scalar(&ComponentControl::data_6, 28),
    };

    /// The six words as a ComponentData for typed access.
    [[nodiscard]] constexpr ComponentData data() const noexcept {
        ComponentData d;
        d.set_words({data_1, data_2, data_3, data_4, data_5, data_6});
        return d;
    }
    constexpr void set_data(const ComponentData& d) noexcept {
        const auto& w = d.words();
        data_1 = std::get<0>(w);
        data_2 = std::get<1>(w);
        data_3 = std::get<2>(w);
        data_4 = std::get<3>(w);
        data_5 = std::get<4>(w);
        data_6 = std::get<5>(w);
    }

    friend constexpr bool operator==(const ComponentControl&,
                                     const ComponentControl&) noexcept = default;
};
static_assert(wire::layout_ok<ComponentControl>());

} // namespace cigi
