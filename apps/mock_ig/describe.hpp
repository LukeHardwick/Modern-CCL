#pragma once

// One-line, human-readable descriptions of packets ("name=value" pairs with units), for the packet
// inspector and for logging. Values the ICD does not define print as `N(undefined)`.

#include <array>
#include <cigi/packet_set.hpp>
#include <cigi/packets.hpp>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace cigi::mock {

/// Names of an enum's values, indexed by the raw value. Specialised below for every enum that
/// describe() prints; `EnumTraits<E>::is_defined` decides whether a value has a name.
template <class E> struct EnumNames;

// NOLINTBEGIN(readability-identifier-naming): the member is read as `EnumNames<E>::names`.
template <> struct EnumNames<IgMode> {
    static constexpr std::array<std::string_view, 4> names{"ResetStandby", "Operate", "Debug",
                                                           "OfflineMaintenance"};
};
template <> struct EnumNames<EarthReferenceModel> {
    static constexpr std::array<std::string_view, 2> names{"Wgs84", "HostDefined"};
};
template <> struct EnumNames<EntityState> {
    static constexpr std::array<std::string_view, 3> names{"InactiveStandby", "Active",
                                                           "Destroyed"};
};
template <> struct EnumNames<AttachState> {
    static constexpr std::array<std::string_view, 2> names{"Detach", "Attach"};
};
template <> struct EnumNames<GroundClamp> {
    static constexpr std::array<std::string_view, 3> names{"NoClamp", "NonConformal", "Conformal"};
};
template <> struct EnumNames<AnimationDirection> {
    static constexpr std::array<std::string_view, 2> names{"Forward", "Backward"};
};
template <> struct EnumNames<AnimationLoopMode> {
    static constexpr std::array<std::string_view, 2> names{"OneShot", "Continuous"};
};
template <> struct EnumNames<AnimationState> {
    static constexpr std::array<std::string_view, 4> names{"Stop", "Pause", "Play", "Continue"};
};
template <> struct EnumNames<RateCoordinateSystem> {
    static constexpr std::array<std::string_view, 2> names{"WorldParent", "Local"};
};
template <> struct EnumNames<ComponentClass> {
    static constexpr std::array<std::string_view, 16> names{
        "Entity",      "View",          "ViewGroup",       "Sensor",
        "RegionalSea", "RegionalTerr",  "RegionalWeather", "GlobalSea",
        "GlobalTerr",  "GlobalWeather", "Atmosphere",      "CelestialSphere",
        "Event",       "System",        "SymbolSurface",   "Symbol"};
};
template <> struct EnumNames<MirrorMode> {
    static constexpr std::array<std::string_view, 4> names{"None", "Horizontal", "Vertical",
                                                           "HorizontalAndVertical"};
};
template <> struct EnumNames<PixelReplicationMode> {
    static constexpr std::array<std::string_view, 8> names{
        "None", "1x2", "2x1", "2x2", "IgDefined4", "IgDefined5", "IgDefined6", "IgDefined7"};
};
template <> struct EnumNames<ProjectionType> {
    static constexpr std::array<std::string_view, 2> names{"Perspective", "OrthographicParallel"};
};
template <> struct EnumNames<HatHotRequestType> {
    static constexpr std::array<std::string_view, 3> names{"Hat", "Hot", "Extended"};
};
template <> struct EnumNames<HatHotCoordinateSystem> {
    static constexpr std::array<std::string_view, 2> names{"Geodetic", "Entity"};
};
template <> struct EnumNames<HatHotResponseType> {
    static constexpr std::array<std::string_view, 2> names{"Hat", "Hot"};
};
// NOLINTEND(readability-identifier-naming)

/// The name of `v`, or `N(undefined)` for a value outside the ICD's definition.
template <class E> [[nodiscard]] std::string enum_name(E v) {
    const auto raw = static_cast<std::size_t>(static_cast<unsigned>(v));
    if (EnumTraits<E>::is_defined(v) && raw < EnumNames<E>::names.size()) {
        return std::string{EnumNames<E>::names.at(raw)};
    }
    return std::format("{}(undefined)", raw);
}

[[nodiscard]] std::string describe(const IgControl& p);
[[nodiscard]] std::string describe(const StartOfFrame& p);
[[nodiscard]] std::string describe(const EntityControl& p);
[[nodiscard]] std::string describe(const ComponentControl& p);
[[nodiscard]] std::string describe(const ArticulatedPartControl& p);
[[nodiscard]] std::string describe(const RateControl& p);
[[nodiscard]] std::string describe(const CelestialSphereControl& p);
[[nodiscard]] std::string describe(const ViewControl& p);
[[nodiscard]] std::string describe(const ViewDefinition& p);
[[nodiscard]] std::string describe(const HatHotRequest& p);
[[nodiscard]] std::string describe(const HatHotResponse& p);
[[nodiscard]] std::string describe(const HatHotExtendedResponse& p);
[[nodiscard]] std::string describe(const UnknownPacket& p);

/// describe() of whichever packet a decoded message item holds.
[[nodiscard]] std::string describe_any(const AnyPacket<HostToIgPackets>& p);
[[nodiscard]] std::string describe_any(const AnyPacket<IgToHostPackets>& p);

} // namespace cigi::mock
