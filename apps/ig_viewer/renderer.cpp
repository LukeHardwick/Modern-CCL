#include "renderer.hpp"

#include "gl_coords.hpp"
#include "mock_ig/terrain.hpp"
#include "terrain_colors.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <raymath.h>
#include <rlgl.h>
#include <string>
#include <vector>

namespace cigi::viewer {

namespace {

using mock::Attitude;
using mock::Entity;
using mock::Mat3;
using mock::Vec3d;

// Terrain tiles: 12 x 12 chunks of 32 x 32 cells of 60 m = +-11.5 km around the scenario origin.
constexpr int chunks_per_side = 12;
constexpr int cells_per_chunk = 32;
constexpr double cell_m = 60.0;
constexpr double half_extent_m = chunks_per_side * cells_per_chunk * cell_m / 2.0;
constexpr float fog_density = 1.0F / 9000.0F;

// PLATFORM: raylib asks for an OpenGL 3.3 core context on macOS; GLSL 330 is the common subset.
constexpr const char* vertex_source = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform vec4 colDiffuse;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 ambientColor;
uniform vec3 camPos;
uniform float fogDensity;
out vec4 vColor;
out vec3 vWorld;
out float vFog;
void main() {
    vec4 w = matModel * vec4(vertexPosition, 1.0);
    vec3 n = normalize(mat3(matModel) * vertexNormal);
    float diffuse = max(dot(n, sunDir), 0.0);
    float sky = 0.6 + 0.4 * (0.5 + 0.5 * n.y);
    vec3 base = vertexColor.rgb * colDiffuse.rgb;
    vColor = vec4(base * (ambientColor * sky + sunColor * diffuse), vertexColor.a * colDiffuse.a);
    vWorld = w.xyz;
    float dist = length(w.xyz - camPos);
    vFog = 1.0 - exp(-pow(dist * fogDensity, 2.0));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

constexpr const char* fragment_source = R"(#version 330
in vec4 vColor;
in vec3 vWorld;
in float vFog;
uniform vec3 fogColor;
uniform float gridOn;
out vec4 finalColor;
void main() {
    vec3 c = vColor.rgb;
    if (gridOn > 0.5) {
        vec2 g = vWorld.xz / 100.0;
        vec2 d = abs(fract(g - 0.5) - 0.5) / fwidth(g);
        float line = 1.0 - min(min(d.x, d.y), 1.0);
        c *= 1.0 - (0.16 * line * (1.0 - vFog));
    }
    c = mix(c, fogColor, vFog);
    finalColor = vec4(c, vColor.a);
}
)";

Color to_color(const Vector3& rgb, unsigned char alpha = 255) noexcept {
    const auto channel = [](float v) {
        return static_cast<unsigned char>(std::clamp(v, 0.0F, 1.0F) * 255.0F + 0.5F);
    };
    return {channel(rgb.x), channel(rgb.y), channel(rgb.z), alpha};
}

Vector3 mix(const Vector3& a, const Vector3& b, float t) noexcept {
    return {a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t)};
}

// ---- mesh authoring (CIGI body axes: x forward, y right, z down) -----------------------------

/// Unindexed triangle soup with per-vertex normals.
class MeshBuilder {
public:
    void triangle(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& na,
                  const Vector3& nb, const Vector3& nc) {
        for (const auto& [p, n] : {std::pair{a, na}, std::pair{b, nb}, std::pair{c, nc}}) {
            vertices_.insert(vertices_.end(), {p.x, p.y, p.z});
            normals_.insert(normals_.end(), {n.x, n.y, n.z});
        }
    }
    /// Flat quad a-b-c-d (counter-clockwise seen from the side the normal points to).
    void quad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d) {
        const Vector3 n =
            Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        triangle(a, b, c, n, n, n);
        triangle(a, c, d, n, n, n);
    }
    [[nodiscard]] Mesh finish() const {
        Mesh mesh{};
        mesh.vertexCount = static_cast<int>(vertices_.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;
        mesh.vertices =
            static_cast<float*>(MemAlloc(static_cast<unsigned>(vertices_.size() * sizeof(float))));
        mesh.normals =
            static_cast<float*>(MemAlloc(static_cast<unsigned>(normals_.size() * sizeof(float))));
        std::copy(vertices_.begin(), vertices_.end(), mesh.vertices);
        std::copy(normals_.begin(), normals_.end(), mesh.normals);
        UploadMesh(&mesh, false);
        return mesh;
    }

private:
    std::vector<float> vertices_;
    std::vector<float> normals_;
};

Mesh make_box(float hx, float hy, float hz) {
    MeshBuilder b;
    const auto v = [&](float sx, float sy, float sz) { return Vector3{sx * hx, sy * hy, sz * hz}; };
    b.quad(v(1, -1, -1), v(1, 1, -1), v(1, 1, 1), v(1, -1, 1));     // +x
    b.quad(v(-1, 1, -1), v(-1, -1, -1), v(-1, -1, 1), v(-1, 1, 1)); // -x
    b.quad(v(-1, 1, -1), v(-1, 1, 1), v(1, 1, 1), v(1, 1, -1));     // +y
    b.quad(v(1, -1, -1), v(1, -1, 1), v(-1, -1, 1), v(-1, -1, -1)); // -y
    b.quad(v(-1, -1, 1), v(1, -1, 1), v(1, 1, 1), v(-1, 1, 1));     // +z
    b.quad(v(-1, 1, -1), v(1, 1, -1), v(1, -1, -1), v(-1, -1, -1)); // -z
    return b.finish();
}

/// Cone along +x from a base circle at x0 to the apex at x1.
Mesh make_cone(float x0, float x1, float radius, int segments) {
    MeshBuilder b;
    const float length = x1 - x0;
    const float norm = std::sqrt((radius * radius) + (length * length));
    const auto ring = [&](float angle) {
        return Vector3{x0, radius * std::cos(angle), radius * std::sin(angle)};
    };
    const auto side_normal = [&](float angle) {
        return Vector3{radius / norm, length * std::cos(angle) / norm,
                       length * std::sin(angle) / norm};
    };
    const Vector3 apex{x1, 0.0F, 0.0F};
    const Vector3 back{-1.0F, 0.0F, 0.0F};
    const Vector3 centre{x0, 0.0F, 0.0F};
    for (int i = 0; i < segments; ++i) {
        const float a0 =
            2.0F * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0F * std::numbers::pi_v<float> * static_cast<float>(i + 1) /
                         static_cast<float>(segments);
        const float am = 0.5F * (a0 + a1);
        b.triangle(ring(a0), ring(a1), apex, side_normal(a0), side_normal(a1), side_normal(am));
        b.triangle(ring(a1), ring(a0), centre, back, back, back);
    }
    return b.finish();
}

// ---- terrain ----------------------------------------------------------------------------------

Mesh make_terrain_chunk(const mock::LocalFrame& frame, int chunk_x, int chunk_y) {
    constexpr int n = cells_per_chunk;
    constexpr int stride = n + 1;
    Mesh mesh{};
    mesh.vertexCount = stride * stride;
    mesh.triangleCount = n * n * 2;
    mesh.vertices =
        static_cast<float*>(MemAlloc(static_cast<unsigned>(mesh.vertexCount) * 3U * sizeof(float)));
    mesh.normals =
        static_cast<float*>(MemAlloc(static_cast<unsigned>(mesh.vertexCount) * 3U * sizeof(float)));
    mesh.colors =
        static_cast<unsigned char*>(MemAlloc(static_cast<unsigned>(mesh.vertexCount) * 4U));
    mesh.indices = static_cast<unsigned short*>(
        MemAlloc(static_cast<unsigned>(mesh.triangleCount) * 3U * sizeof(unsigned short)));

    const double x0 = -half_extent_m + (chunk_x * n * cell_m);
    const double y0 = -half_extent_m + (chunk_y * n * cell_m);
    std::size_t vi = 0;
    std::size_t ci = 0;
    for (int j = 0; j < stride; ++j) {
        for (int i = 0; i < stride; ++i) {
            const double east = x0 + (i * cell_m);
            const double north = y0 + (j * cell_m);
            const mock::Geodetic g = frame.to_geodetic({east, north, 0.0});
            const double h = mock::terrain_height(g.lat_deg, g.lon_deg);
            const Vector3 p = to_gl({east, north, h - frame.origin().alt_m});
            const Vector3 nrm = to_gl(mock::terrain_normal(g.lat_deg, g.lon_deg).enu);
            mesh.vertices[vi] = p.x;
            mesh.vertices[vi + 1] = p.y;
            mesh.vertices[vi + 2] = p.z;
            mesh.normals[vi] = nrm.x;
            mesh.normals[vi + 1] = nrm.y;
            mesh.normals[vi + 2] = nrm.z;
            vi += 3;
            const Rgb rgb = terrain_rgb(h);
            mesh.colors[ci] = static_cast<unsigned char>(rgb.at(0) * 255.0F);
            mesh.colors[ci + 1] = static_cast<unsigned char>(rgb.at(1) * 255.0F);
            mesh.colors[ci + 2] = static_cast<unsigned char>(rgb.at(2) * 255.0F);
            mesh.colors[ci + 3] = 255;
            ci += 4;
        }
    }
    std::size_t ti = 0;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const auto a = static_cast<unsigned short>((j * stride) + i);
            const auto b = static_cast<unsigned short>(a + 1);
            const auto c = static_cast<unsigned short>(a + stride);
            const auto d = static_cast<unsigned short>(c + 1);
            // Counter-clockwise seen from above (+y in raylib): east is +x, north is -z.
            for (const unsigned short idx : {a, c, b, b, c, d}) {
                mesh.indices[ti++] = idx;
            }
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}

} // namespace

// ---- sky --------------------------------------------------------------------------------------

SkyState compute_sky(const mock::SceneModel& scene) {
    SkyState s;
    const mock::CelestialState& c = scene.celestial();
    const auto date = c.date.value_or(std::chrono::year_month_day{
        std::chrono::year{2026}, std::chrono::June, std::chrono::day{21}});
    const mock::Geodetic& o = scene.config().origin;
    s.sun = mock::sun_position(date, c.utc_hours(), o.lat_deg, o.lon_deg);
    s.daylight = static_cast<float>(mock::daylight_factor(s.sun.elevation_deg));
    if (!c.sun) {
        s.daylight = 0.0F; // the Host switched the sun off
    }
    const double el = s.sun.elevation_deg * mock::deg_to_rad;
    const double az = s.sun.azimuth_deg * mock::deg_to_rad;
    s.sun_direction =
        to_gl({std::cos(el) * std::sin(az), std::cos(el) * std::cos(az), std::sin(el)});

    constexpr Vector3 day_zenith{0.22F, 0.42F, 0.82F};
    constexpr Vector3 day_horizon{0.66F, 0.79F, 0.92F};
    constexpr Vector3 night_zenith{0.008F, 0.012F, 0.04F};
    constexpr Vector3 night_horizon{0.03F, 0.04F, 0.09F};
    constexpr Vector3 dusk{1.0F, 0.55F, 0.28F};
    Vector3 zenith = mix(night_zenith, day_zenith, s.daylight);
    Vector3 horizon = mix(night_horizon, day_horizon, s.daylight);
    // Warm band near the horizon while the sun is within a few degrees of it.
    const auto dusk_amount =
        static_cast<float>(std::clamp(1.0 - (std::abs(s.sun.elevation_deg) / 9.0), 0.0, 1.0));
    horizon = mix(horizon, dusk, 0.55F * dusk_amount * (c.sun ? 1.0F : 0.0F));
    s.zenith = to_color(zenith);
    s.horizon = to_color(horizon);
    s.fog = horizon;
    const float sun_up = std::clamp(static_cast<float>(s.sun.elevation_deg) / 12.0F, 0.0F, 1.0F);
    s.sun_color =
        mix({0.0F, 0.0F, 0.0F}, mix(dusk, {1.0F, 0.96F, 0.88F}, sun_up), s.daylight * sun_up);
    s.ambient = mix({0.07F, 0.08F, 0.14F}, {0.42F, 0.46F, 0.52F}, s.daylight);
    s.star_intensity =
        c.star_field ? (c.star_field_intensity / 100.0F) * (1.0F - s.daylight) : 0.0F;
    return s;
}

// ---- cameras ----------------------------------------------------------------------------------

bool find_host_view(const mock::SceneModel& scene, std::uint16_t& view_id) {
    if (scene.find_view(0) != nullptr && scene.find_view(0)->controlled) {
        view_id = 0;
        return true;
    }
    for (const auto& [id, v] : scene.views()) {
        if (v.controlled) {
            view_id = id;
            return true;
        }
    }
    return false;
}

CameraResult make_camera(const mock::SceneModel& scene, const CameraState& state, float aspect) {
    CameraResult r;
    r.camera.projection = CAMERA_PERSPECTIVE;
    const auto look = [&](const Vec3d& position, const Vec3d& forward_enu, const Vec3d& up_enu,
                          double h_fov) {
        r.position = position;
        r.camera.position = to_gl(position);
        r.camera.target = to_gl(position + (forward_enu * 100.0));
        r.camera.up = to_gl(up_enu);
        r.yaw_deg = mock::wrap_360(std::atan2(forward_enu.x, forward_enu.y) * mock::rad_to_deg);
        r.horizontal_fov_deg = h_fov;
        // The Host defines the horizontal field of view; derive the vertical one for this window.
        const double half = std::tan(h_fov * mock::deg_to_rad / 2.0);
        r.camera.fovy = static_cast<float>(std::clamp(
            2.0 * std::atan(half / static_cast<double>(aspect)) * mock::rad_to_deg, 10.0, 120.0));
    };

    CameraKind mode = state.mode;
    std::uint16_t view_id = 0;
    if (mode == CameraKind::Host) {
        const bool have_view =
            find_host_view(scene, view_id) && scene.view_pose(view_id).has_value();
        if (!have_view) {
            mode = CameraKind::Free;
        }
    }
    if (mode == CameraKind::Host) {
        const mock::ViewState& v = *scene.find_view(view_id);
        const mock::ChildPose pose = *scene.view_pose(view_id);
        look(pose.position_enu, pose.body_to_enu.c0, pose.body_to_enu.c2 * -1.0,
             v.horizontal_fov_deg());
        r.near_m = static_cast<double>(v.near_plane);
        r.far_m = static_cast<double>(v.far_plane);
        r.host_view = true;
    } else if (mode == CameraKind::Chase) {
        const Entity* e = scene.find(state.chase_target);
        const Vec3d centre = e != nullptr ? e->enu : Vec3d{};
        const double heading = e != nullptr ? e->world_attitude.yaw_deg : 0.0;
        const double azimuth =
            (heading + 180.0 + static_cast<double>(state.chase_yaw_deg)) * mock::deg_to_rad;
        const double pitch = static_cast<double>(state.chase_pitch_deg) * mock::deg_to_rad;
        const double dist = static_cast<double>(state.chase_distance_m);
        const Vec3d offset{dist * std::cos(pitch) * std::sin(azimuth),
                           dist * std::cos(pitch) * std::cos(azimuth), dist * std::sin(pitch)};
        look(centre + offset, mock::normalized(offset * -1.0), {0.0, 0.0, 1.0}, 60.0);
        r.near_m = 1.0;
    } else {
        const double yaw = static_cast<double>(state.free_yaw_deg) * mock::deg_to_rad;
        const double pitch = static_cast<double>(state.free_pitch_deg) * mock::deg_to_rad;
        const Vec3d forward{std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch),
                            std::sin(pitch)};
        look(state.free_position, forward, {0.0, 0.0, 1.0}, 60.0);
        r.near_m = 1.0;
    }
    return r;
}

// ---- Renderer ---------------------------------------------------------------------------------

Renderer::~Renderer() {
    if (!ready_) {
        return;
    }
    for (const Mesh& m : terrain_) {
        UnloadMesh(m);
    }
    for (const Mesh& m : {fuselage_, wing_, fin_, body_, cab_, nose_, sphere_, arm_, cross_}) {
        UnloadMesh(m);
    }
    UnloadShader(shader_);
}

void Renderer::init(const mock::SceneModel& scene) {
    shader_ = LoadShaderFromMemory(vertex_source, fragment_source);
    loc_sun_dir_ = GetShaderLocation(shader_, "sunDir");
    loc_sun_color_ = GetShaderLocation(shader_, "sunColor");
    loc_ambient_ = GetShaderLocation(shader_, "ambientColor");
    loc_cam_pos_ = GetShaderLocation(shader_, "camPos");
    loc_fog_density_ = GetShaderLocation(shader_, "fogDensity");
    loc_fog_color_ = GetShaderLocation(shader_, "fogColor");
    loc_grid_ = GetShaderLocation(shader_, "gridOn");
    material_ = LoadMaterialDefault();
    material_.shader = shader_;

    origin_ = {};
    terrain_.reserve(static_cast<std::size_t>(chunks_per_side) * chunks_per_side);
    for (int cy = 0; cy < chunks_per_side; ++cy) {
        for (int cx = 0; cx < chunks_per_side; ++cx) {
            terrain_.push_back(make_terrain_chunk(scene.frame(), cx, cy));
        }
    }

    // Aircraft: ~8 m cone, 6 m half-span wings, a fin. Vehicles: boxes sitting on the reference
    // point (z is down, so "up" is -z). Parts: an arm along +x with a cross-bar, so both yaw/pitch
    // (the arm swings) and roll (the cross-bar spins) are visible.
    fuselage_ = make_cone(-4.0F, 4.0F, 1.1F, 16);
    wing_ = make_box(1.1F, 6.0F, 0.12F);
    fin_ = make_box(0.9F, 0.1F, 1.1F);
    body_ = make_box(2.6F, 1.15F, 0.55F);
    cab_ = make_box(1.1F, 1.0F, 0.45F);
    nose_ = make_cone(0.0F, 2.6F, 0.55F, 12);
    sphere_ = GenMeshSphere(1.6F, 12, 16);
    arm_ = make_box(0.8F, 0.1F, 0.1F);
    cross_ = make_box(0.08F, 0.55F, 0.08F);
    ready_ = true;
}

void Renderer::draw_background(const SkyState& sky, const CameraResult& cam, int width,
                               int height) const {
    // Horizon line: where a distant point straight ahead on the horizontal plane lands.
    const double yaw = cam.yaw_deg * mock::deg_to_rad;
    const Vec3d far_point =
        cam.position + Vec3d{std::sin(yaw) * 80000.0, std::cos(yaw) * 80000.0, -cam.position.z};
    const Vector2 h = GetWorldToScreen(to_gl(far_point), cam.camera);
    const int horizon_y = std::clamp(static_cast<int>(h.y), 0, height);
    ClearBackground(sky.horizon);
    DrawRectangleGradientV(0, 0, width, std::max(horizon_y, 1), sky.zenith, sky.horizon);

    if (sky.star_intensity > 0.01F) {
        // Fixed pseudo-random stars (a hash of the index), fixed on screen: a backdrop, not an
        // ephemeris.
        for (std::uint32_t i = 0; i < 160; ++i) {
            const std::uint32_t hsh = (i + 1U) * 2654435761U;
            const int x = static_cast<int>(hsh % static_cast<std::uint32_t>(width));
            const int y =
                static_cast<int>((hsh >> 11U) % static_cast<std::uint32_t>(std::max(horizon_y, 1)));
            DrawPixel(
                x, y,
                Fade(WHITE, sky.star_intensity *
                                (0.4F + (0.6F * static_cast<float>((hsh >> 5U) % 100U) / 100.0F))));
        }
    }
    if (sky.sun.elevation_deg > -3.0) {
        const Vector3 sun_world =
            Vector3Add(cam.camera.position, Vector3Scale(sky.sun_direction, 50000.0F));
        const Vector3 forward =
            Vector3Normalize(Vector3Subtract(cam.camera.target, cam.camera.position));
        if (Vector3DotProduct(forward, sky.sun_direction) > 0.0F) {
            const Vector2 p = GetWorldToScreen(sun_world, cam.camera);
            DrawCircleV(p, 22.0F, Fade(Color{255, 244, 200, 255}, 0.25F));
            DrawCircleV(p, 11.0F, Color{255, 248, 220, 255});
        }
    }
}

void Renderer::draw_mesh(const Mesh& mesh, const Vec3d& base_position, const Mat3& base_body,
                         const Vec3d& offset, const Attitude& attitude, Color color, float scale) {
    const mock::ChildPose p = mock::compose_child(base_position, base_body,
                                                  offset * static_cast<double>(scale), attitude);
    material_.maps[MATERIAL_MAP_DIFFUSE].color = color;
    DrawMesh(mesh, material_, pose_matrix(p.position_enu, p.body_to_enu, scale));
}

void Renderer::draw_entity(const Entity& e, mock::EntityCategory category,
                           const DrawOptions& options, bool selected) {
    Color c = id_color(e.id);
    c.a = e.apparent_alpha;
    const float s = options.shape_scale;
    const Attitude none{};
    switch (category) {
    case mock::EntityCategory::Aircraft:
        draw_mesh(fuselage_, e.enu, e.body_to_enu, {}, none, c, s);
        draw_mesh(wing_, e.enu, e.body_to_enu, {-0.4, 0.0, 0.0}, none, ColorBrightness(c, -0.15F),
                  s);
        draw_mesh(fin_, e.enu, e.body_to_enu, {-3.3, 0.0, -1.0}, none, ColorBrightness(c, -0.15F),
                  s);
        break;
    case mock::EntityCategory::GroundVehicle:
        draw_mesh(body_, e.enu, e.body_to_enu, {0.0, 0.0, -0.7}, none, c, s);
        draw_mesh(cab_, e.enu, e.body_to_enu, {-0.5, 0.0, -1.65}, none, ColorBrightness(c, -0.2F),
                  s);
        draw_mesh(nose_, e.enu, e.body_to_enu, {2.6, 0.0, -0.7}, none, Color{245, 245, 245, c.a},
                  s);
        break;
    case mock::EntityCategory::Sea:
    case mock::EntityCategory::Lifeform:
    case mock::EntityCategory::Munition:
    case mock::EntityCategory::Other:
        draw_mesh(sphere_, e.enu, e.body_to_enu, {}, none, c, s);
        draw_mesh(nose_, e.enu, e.body_to_enu, {1.6, 0.0, 0.0}, none, Color{245, 245, 245, c.a}, s);
        break;
    case mock::EntityCategory::None: {
        // "Null" entities have no geometry: show their axes (forward red, right green, down blue).
        const float len = 6.0F * s;
        const Vector3 p = to_gl(e.enu);
        DrawLine3D(p, to_gl(e.enu + (e.body_to_enu.c0 * static_cast<double>(len))), RED);
        DrawLine3D(p, to_gl(e.enu + (e.body_to_enu.c1 * static_cast<double>(len))), GREEN);
        DrawLine3D(p, to_gl(e.enu + (e.body_to_enu.c2 * static_cast<double>(len))), BLUE);
        break;
    }
    }

    if (options.parts) {
        const Color part_color = ColorBrightness(c, 0.25F);
        for (const auto& [part_id, part] : e.parts) {
            if (!part.enabled) {
                continue;
            }
            // Part frame: the entity body frame moved to the part offset and rotated by its angles.
            const mock::ChildPose pose = mock::compose_child(
                e.enu, e.body_to_enu, part.offset * static_cast<double>(s), part.attitude);
            draw_mesh(arm_, pose.position_enu, pose.body_to_enu, {0.8, 0.0, 0.0}, none, part_color,
                      s);
            draw_mesh(cross_, pose.position_enu, pose.body_to_enu, {1.6, 0.0, 0.0}, none,
                      part_color, s);
        }
    }

    if (selected) {
        const float size = 8.0F * s;
        DrawCubeWiresV(to_gl(e.enu), {size, size, size}, YELLOW);
    }
}

void Renderer::draw_world(const mock::SceneModel& scene, const SkyState& sky,
                          const CameraResult& cam, const DrawOptions& options,
                          std::uint16_t selected, bool hide_view_entity) {
    rlDisableBackfaceCulling();

    const auto set3 = [&](int loc, const Vector3& v) {
        SetShaderValue(shader_, loc, &v, SHADER_UNIFORM_VEC3);
    };
    set3(loc_sun_dir_, sky.sun_direction);
    set3(loc_sun_color_, sky.sun_color);
    set3(loc_ambient_, sky.ambient);
    set3(loc_cam_pos_, cam.camera.position);
    set3(loc_fog_color_, sky.fog);
    const float density = fog_density;
    SetShaderValue(shader_, loc_fog_density_, &density, SHADER_UNIFORM_FLOAT);
    const float grid = options.terrain_grid ? 1.0F : 0.0F;
    SetShaderValue(shader_, loc_grid_, &grid, SHADER_UNIFORM_FLOAT);

    material_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    for (const Mesh& chunk : terrain_) {
        DrawMesh(chunk, material_, MatrixIdentity());
    }
    const float no_grid = 0.0F;
    SetShaderValue(shader_, loc_grid_, &no_grid, SHADER_UNIFORM_FLOAT);

    labels_.clear();
    std::uint16_t view_entity = 0;
    bool skip_view_entity = false;
    if (hide_view_entity) {
        std::uint16_t view_id = 0;
        if (find_host_view(scene, view_id)) {
            view_entity = scene.find_view(view_id)->entity_id;
            skip_view_entity = true;
        }
    }

    for (const auto& [id, e] : scene.entities()) {
        if (!e.visible) {
            continue;
        }
        const Color c = id_color(id);
        if (options.trails && e.trail.size() > 1) {
            // Newest 150 points, every second one: enough to read the path, cheap for hundreds.
            const std::size_t count = e.trail.size();
            const std::size_t first = count > 150 ? count - 150 : 0;
            for (std::size_t i = first + 2; i < count; i += 2) {
                const float age = static_cast<float>(i - first) / static_cast<float>(count - first);
                DrawLine3D(to_gl(e.trail.at(i - 2)), to_gl(e.trail.at(i)),
                           Fade(c, 0.15F + (0.6F * age)));
            }
        }
        if (skip_view_entity && id == view_entity) {
            continue;
        }
        if (options.drop_lines && e.agl_m > 3.0) {
            const Vec3d ground{e.enu.x, e.enu.y, e.terrain_m - scene.frame().origin().alt_m};
            DrawLine3D(to_gl(e.enu), to_gl(ground), Fade(c, 0.5F));
        }
        draw_entity(e, scene.category_of(e.entity_type), options, id == selected);
        labels_.push_back(
            {e.enu + Vec3d{0.0, 0.0, 5.0 * static_cast<double>(options.shape_scale)}, id});
    }

    // HAT/HOT probes of the last two seconds: terrain point and the requested point above it.
    for (const mock::HatHotProbe& p : scene.probes()) {
        if (scene.frame_count() > 120 && p.ig_frame + 120 < scene.frame_count()) {
            continue;
        }
        const Vec3d at = scene.frame().to_enu(p.point);
        if (p.valid) {
            const Vec3d ground{at.x, at.y, p.hot_m - scene.frame().origin().alt_m};
            DrawLine3D(to_gl(ground), to_gl(at), Fade(LIME, 0.8F));
            DrawSphere(to_gl(ground), 1.2F, LIME);
            DrawSphere(to_gl(at), 0.8F, Fade(LIME, 0.8F));
        } else {
            DrawSphere(to_gl(at), 2.5F, RED);
        }
    }
    rlEnableBackfaceCulling();
}

void Renderer::draw_overlay(const CameraResult& cam, const SkyState& sky,
                            const mock::SceneModel& scene, const CameraState& state,
                            const DrawOptions& options, std::uint16_t selected) const {
    if (options.labels) {
        const Vector3 forward =
            Vector3Normalize(Vector3Subtract(cam.camera.target, cam.camera.position));
        // Greedy de-overlap: the selected entity is placed first, then the rest in ID order; a
        // label that would overlap an earlier one is lifted in steps and joined to its anchor by
        // a leader line, so adjacent entities (e.g. 3 and 4) stay legible.
        struct Placed {
            int x, y, w, h;
        };
        std::vector<Placed> placed;
        placed.reserve(labels_.size());
        const auto overlaps = [&placed](int x, int y, int w, int h) {
            return std::ranges::any_of(placed, [&](const Placed& q) {
                return x < q.x + q.w && q.x < x + w && y < q.y + q.h && q.y < y + h;
            });
        };
        const auto draw_label = [&](const Label& l) {
            const Vector3 p = to_gl(l.position);
            if (Vector3DotProduct(Vector3Subtract(p, cam.camera.position), forward) <= 1.0F) {
                return; // behind the camera
            }
            const Vector2 s = GetWorldToScreen(p, cam.camera);
            const std::string text = l.id == 0 ? "0 ownship" : std::to_string(l.id);
            const int size = l.id == selected ? 20 : 16;
            const int w = MeasureText(text.c_str(), size) + 6;
            const int h = size + 2;
            const int x = static_cast<int>(s.x) - (w / 2);
            int y = static_cast<int>(s.y) - h - 4;
            int steps = 0;
            bool found = !overlaps(x, y, w, h);
            while (!found && steps < 12) {
                y -= h + 1;
                ++steps;
                found = !overlaps(x, y, w, h);
            }
            if (!found) {
                return; // no room: drop the label rather than draw an unreadable pile
            }
            placed.push_back({x, y, w, h});
            const Color c = id_color(l.id, 0.45F, 1.0F);
            if (steps > 0) {
                DrawLine(static_cast<int>(s.x), static_cast<int>(s.y), static_cast<int>(s.x), y + h,
                         Fade(c, 0.8F));
            }
            DrawRectangle(x, y, w, h, Fade(BLACK, 0.45F));
            DrawText(text.c_str(), x + 3, y + 1, size, c);
        };
        for (const Label& l : labels_) {
            if (l.id == selected) {
                draw_label(l);
            }
        }
        for (const Label& l : labels_) {
            if (l.id != selected) {
                draw_label(l);
            }
        }
    }
    const char* mode = state.mode == CameraKind::Host    ? "Host view"
                       : state.mode == CameraKind::Chase ? "Chase"
                                                         : "Free";
    if (state.mode == CameraKind::Host && !cam.host_view) {
        mode = "Host view (no view defined yet: free camera)";
    }
    const mock::CelestialState& c = scene.celestial();
    const std::string hud =
        std::format("{}   UTC {:02}:{:02}   sun {:.0f} deg   {} entities", mode, c.hour, c.minute,
                    sky.sun.elevation_deg, scene.entities().size());
    // Centred at the bottom: the side columns of panels (see mock::compute_panel_layout) cover the
    // corners. Logical screen points, like every 2-D draw call here.
    const int hud_x = std::max((GetScreenWidth() - MeasureText(hud.c_str(), 20)) / 2, 4);
    DrawText(hud.c_str(), hud_x + 2, GetScreenHeight() - 30, 20, Fade(BLACK, 0.85F));
    DrawText(hud.c_str(), hud_x, GetScreenHeight() - 32, 20, WHITE);
}

} // namespace cigi::viewer
