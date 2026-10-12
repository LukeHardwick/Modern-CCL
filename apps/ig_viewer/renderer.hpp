#pragma once

// The 3-D out-the-window view: sky, lit terrain, entity shapes, trails, labels. All the scene
// knowledge comes from mock::SceneModel; nothing here talks CIGI.

#include "app.hpp"
#include "mock_ig/sky.hpp"

#include <cstdint>
#include <raylib.h>
#include <vector>

namespace cigi::viewer {

/// Sky colours and light derived from the Celestial Sphere Control time (see mock_ig/sky.hpp).
struct SkyState {
    mock::SunPosition sun;
    float daylight = 1.0F;                   ///< 0 night .. 1 day.
    Vector3 sun_direction{0.0F, 1.0F, 0.0F}; ///< Unit vector towards the sun, raylib axes.
    Color zenith{};
    Color horizon{};
    Vector3 sun_color{1.0F, 1.0F, 1.0F};
    Vector3 ambient{0.3F, 0.3F, 0.3F};
    Vector3 fog{0.7F, 0.8F, 0.9F};
    float star_intensity = 0.0F; ///< 0..1, already faded by daylight.
};

[[nodiscard]] SkyState compute_sky(const mock::SceneModel& scene);

struct CameraResult {
    Camera3D camera{};
    double near_m = 1.0;
    double far_m = 25000.0;
    bool host_view = false; ///< The camera follows a Host-defined view.
    mock::Vec3d position{}; ///< Eye in the local ENU frame.
    double yaw_deg = 0.0;   ///< Look direction, clockwise from north.
    double horizontal_fov_deg = 60.0;
};

/// Build the camera for the current mode. If Host view is requested but the Host has defined no
/// usable view yet, the free camera is used instead (`host_view` is false then).
[[nodiscard]] CameraResult make_camera(const mock::SceneModel& scene, const CameraState& state,
                                       float aspect);

/// The Host-defined view shown in Host view mode: view 0 if it exists, otherwise the first view a
/// View Control has addressed. Returns false if there is none.
[[nodiscard]] bool find_host_view(const mock::SceneModel& scene, std::uint16_t& view_id);

class Renderer {
public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    ~Renderer();

    /// Create GPU resources (after InitWindow): shader, entity meshes, terrain tiles.
    void init(const mock::SceneModel& scene);

    /// Sky gradient, sun disc and stars (2-D, before BeginMode3D).
    void draw_background(const SkyState& sky, const CameraResult& cam, int width, int height) const;

    /// Terrain, entities, trails, probes (between BeginMode3D and EndMode3D).
    void draw_world(const mock::SceneModel& scene, const SkyState& sky, const CameraResult& cam,
                    const DrawOptions& options, std::uint16_t selected, bool hide_view_entity);

    /// ID labels and the heads-up text (2-D, after EndMode3D).
    void draw_overlay(const CameraResult& cam, const SkyState& sky, const mock::SceneModel& scene,
                      const CameraState& state, const DrawOptions& options,
                      std::uint16_t selected) const;

private:
    struct Label {
        mock::Vec3d position;
        std::uint16_t id = 0;
    };

    void draw_entity(const mock::Entity& e, const DrawOptions& options, bool selected);
    void draw_mesh(const Mesh& mesh, const mock::Vec3d& base_position, const mock::Mat3& base_body,
                   const mock::Vec3d& offset, const mock::Attitude& attitude, Color color,
                   float scale);

    bool ready_ = false;
    Shader shader_{};
    Material material_{};
    int loc_sun_dir_ = -1;
    int loc_sun_color_ = -1;
    int loc_ambient_ = -1;
    int loc_cam_pos_ = -1;
    int loc_fog_density_ = -1;
    int loc_fog_color_ = -1;
    int loc_grid_ = -1;
    std::vector<Mesh> terrain_;
    Mesh fuselage_{};
    Mesh wing_{};
    Mesh fin_{};
    Mesh body_{};
    Mesh cab_{};
    Mesh nose_{};
    Mesh sphere_{};
    Mesh arm_{};
    Mesh cross_{};
    mock::Vec3d origin_{};
    mutable std::vector<Label> labels_;
};

} // namespace cigi::viewer
