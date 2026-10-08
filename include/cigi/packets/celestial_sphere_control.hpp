#pragma once

// Celestial Sphere Control packet (ICD 4.1.9), opcode 9, Host -> IG, 16 bytes. Sky model: time of
// day, date, sun / moon / star field, star field intensity.
//
//   byte 0 opcode (9)   byte 1 size (16)   byte 2 hour (u8)   byte 3 minute (u8)
//   byte 4 bits 7-5 reserved, 4 date/time valid, 3 star field enable, 2 moon enable, 1 sun enable,
//          0 ephemeris model enable
//   bytes 5-7 reserved   bytes 8-11 date (u32, MMDDYYYY decimal)   bytes 12-15 star field intensity
//
// Appendix B (3.3): Hour and Minute are UTC; earlier versions used local time.

#include "cigi/packet.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>

namespace cigi {

/// Date field encoding (ICD 4.1.9): (month * 1000000) + (day * 10000) + year, e.g. 8 Oct 2026 is
/// 10082026 (the ICD calls it a seven- or eight-digit number).
/// Returns nullopt if the date is not a valid calendar date or its year is outside 0..9999.
[[nodiscard]] constexpr std::optional<std::uint32_t>
pack_celestial_date(std::chrono::year_month_day d) noexcept {
    const int year = static_cast<int>(d.year());
    if (!d.ok() || year < 0 || year > 9999) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>((static_cast<unsigned>(d.month()) * 1'000'000U) +
                                      (static_cast<unsigned>(d.day()) * 10'000U) +
                                      static_cast<unsigned>(year));
}

/// Inverse of pack_celestial_date. Returns nullopt for values that are not a valid MMDDYYYY
/// calendar date.
[[nodiscard]] constexpr std::optional<std::chrono::year_month_day>
unpack_celestial_date(std::uint32_t packed) noexcept {
    const std::uint32_t month = packed / 1'000'000U;
    const std::uint32_t day = (packed / 10'000U) % 100U;
    const std::uint32_t year = packed % 10'000U;
    // Check the ranges first: std::chrono::month / day silently truncate to 8 bits.
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return std::nullopt;
    }
    const std::chrono::year_month_day d{std::chrono::year{static_cast<int>(year)},
                                        std::chrono::month{month}, std::chrono::day{day}};
    if (!d.ok()) {
        return std::nullopt;
    }
    return d;
}

struct CelestialSphereControl {
    static constexpr std::uint8_t opcode = 9;
    static constexpr Direction direction = Direction::HostToIg;
    static constexpr std::size_t wire_size = 16;

    std::uint8_t hour = 0;               ///< 0..23, UTC (3.3).
    std::uint8_t minute = 0;             ///< 0..59, UTC (3.3).
    bool ephemeris_model_enabled = true; ///< false: static time of day; true: IG advances it.
    bool sun_enabled = false;
    bool moon_enabled = false;
    bool star_field_enabled = false;
    bool date_time_valid = false;   ///< Hour, Minute and Date override the IG's date and time.
    std::uint32_t date = 0;         ///< MMDDYYYY; see pack_celestial_date / unpack_celestial_date.
    float star_field_intensity = 0; ///< Percent, 0..100; ignored if the star field is disabled.

    /// Store `d` in `date`. Returns false and leaves `date` unchanged if `d` is not encodable.
    constexpr bool set_date(std::chrono::year_month_day d) noexcept {
        const auto packed = pack_celestial_date(d);
        if (packed) {
            date = *packed;
        }
        return packed.has_value();
    }

    /// `date` as a calendar date; nullopt if it is not a valid MMDDYYYY value.
    [[nodiscard]] constexpr std::optional<std::chrono::year_month_day>
    calendar_date() const noexcept {
        return unpack_celestial_date(date);
    }

    static constexpr auto fields = std::tuple{
        wire::scalar(&CelestialSphereControl::hour, 2).range(0, 23),
        wire::scalar(&CelestialSphereControl::minute, 3).range(0, 59),
        wire::flag(&CelestialSphereControl::ephemeris_model_enabled, 4, 0),
        wire::flag(&CelestialSphereControl::sun_enabled, 4, 1),
        wire::flag(&CelestialSphereControl::moon_enabled, 4, 2),
        wire::flag(&CelestialSphereControl::star_field_enabled, 4, 3),
        wire::flag(&CelestialSphereControl::date_time_valid, 4, 4),
        wire::scalar(&CelestialSphereControl::date, 8),
        wire::scalar(&CelestialSphereControl::star_field_intensity, 12).range(0.0F, 100.0F),
    };

    friend constexpr bool operator==(const CelestialSphereControl&,
                                     const CelestialSphereControl&) noexcept = default;
};
static_assert(wire::layout_ok<CelestialSphereControl>());

/// Field ranges (hour, minute, star field intensity), plus: when Date/Time Valid is set, Date must
/// be a valid MMDDYYYY calendar date (error offset 8). The Date is not checked otherwise.
[[nodiscard]] constexpr Result<void> validate(const CelestialSphereControl& p) noexcept {
    if (auto ok = wire::validate_fields(p); !ok) {
        return ok;
    }
    if (p.date_time_valid && !unpack_celestial_date(p.date)) {
        return std::unexpected(Error{
            .code = ErrorCode::InvalidFieldValue,
            .offset = 8,
            .opcode = CelestialSphereControl::opcode,
        });
    }
    return {};
}

} // namespace cigi
