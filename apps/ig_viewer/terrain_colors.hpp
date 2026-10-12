#pragma once

// Colour of the synthetic terrain by height: water below 70 m, grass, rock above 130 m
// (the mock IG's material bands), blended over a few metres so the bands do not look like steps.

#include <algorithm>
#include <array>

namespace cigi::viewer {

using Rgb = std::array<float, 3>;

[[nodiscard]] inline float smooth_step(float edge0, float edge1, float x) noexcept {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] inline Rgb terrain_rgb(double height_m) noexcept {
    constexpr Rgb water{0.16F, 0.36F, 0.60F};
    constexpr Rgb grass{0.30F, 0.56F, 0.26F};
    constexpr Rgb rock{0.58F, 0.52F, 0.46F};
    const auto h = static_cast<float>(height_m);
    const float to_grass = smooth_step(66.0F, 74.0F, h);
    const float to_rock = smooth_step(126.0F, 134.0F, h);
    Rgb out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float low = water.at(i) + ((grass.at(i) - water.at(i)) * to_grass);
        out.at(i) = low + ((rock.at(i) - low) * to_rock);
    }
    return out;
}

} // namespace cigi::viewer
