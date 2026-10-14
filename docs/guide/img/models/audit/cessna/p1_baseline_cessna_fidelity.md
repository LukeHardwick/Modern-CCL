# Fidelity audit: p1_baseline_cessna

Model `cessna.glb` (1.20 MB, sha256 `fc5a6a5617ea`), config `cessna_fidelity.toml` (sha256 `dc646014a332`, status: CALIBRATED, pending user approval), family **textured**. Coordinates: metres, x = vehicle left, y = aft (nose at -y), z = up.

**28 of 118 gates pass; 78 fail, 12 not applicable or not measurable; 48 hard failures.** Overall: **FAIL (hard)**.

| Group | Pass | Total |
|---|---|---|
| F1 | 6 | 29 |
| F2 | 0 | 43 |
| F3 | 15 | 18 |
| F4 | 4 | 18 |
| F5 | 3 | 10 |

| Gate | Value | Limit | Result |
|---|---|---|---|
| F1.side.iou: side silhouette IoU | 0.702 | >= 0.920 | **FAIL (hard)** |
| F1.side.outline_p95: side outline distance p95 | 534.25 | <= 30 | **FAIL (hard)** |
| F1.side.outline_max: side outline distance max | 753.26 | <= 80 | **FAIL (hard)** |
| F1.station.spinner_tip: station spinner_tip (side) | 0 | <= 40 | PASS |
| F1.station.rudder_te: station rudder_te (side) | 0 | <= 40 | PASS |
| F1.station.fin_top: station fin_top (side) | 0 | <= 40 | PASS |
| F1.station.belly_line: station belly_line (side) | 60.7 | <= 40 | **FAIL (hard)** |
| F1.station.cowl_front: station cowl_front (side) | 117.1 | <= 40 | **FAIL (hard)** |
| F1.station.cowl_top: station cowl_top (side) | 180 | <= 40 | **FAIL (hard)** |
| F1.station.windscreen_base: station windscreen_base (side) | 562.9 | <= 40 | **FAIL (hard)** |
| F1.station.cabin_roof: station cabin_roof (side) | 202.9 | <= 40 | **FAIL (hard)** |
| F1.station.fin_leading_edge: station fin_leading_edge (side) | 2840 | <= 40 | **FAIL (hard)** |
| F1.station.nose_wheel_centre: station nose_wheel_centre (side) | 377.5 | <= 40 | **FAIL (hard)** |
| F1.station.main_wheel_centre: station main_wheel_centre (side) | 265 | <= 40 | **FAIL (hard)** |
| F1.top.iou: top silhouette IoU | 0.739 | >= 0.950 | **FAIL (hard)** |
| F1.top.outline_p95: top outline distance p95 | 480 | <= 30 | **FAIL (hard)** |
| F1.top.outline_max: top outline distance max | 620 | <= 80 | **FAIL (hard)** |
| F1.station.wing_le_root: station wing_le_root (top) | 235 | <= 40 | **FAIL (hard)** |
| F1.station.wing_le_tip: station wing_le_tip (top) | 641.1 | <= 40 | **FAIL (hard)** |
| F1.station.wing_te_root: station wing_te_root (top) | 25 | <= 40 | PASS |
| F1.station.wing_te_tip: station wing_te_tip (top) | 511.8 | <= 40 | **FAIL (hard)** |
| F1.station.tailplane_tip: station tailplane_tip (top) | 85 | <= 40 | **FAIL (hard)** |
| F1.station.tailplane_te: station tailplane_te (top) | 192.9 | <= 40 | **FAIL (hard)** |
| F1.front.iou: front silhouette IoU | 0.255 | >= 0.720 | **FAIL (hard)** |
| F1.front.outline_p95: front outline distance p95 | 535 | <= 30 | **FAIL (hard)** |
| F1.front.outline_max: front outline distance max | 550.11 | <= 80 | **FAIL (hard)** |
| F1.station.wing_tip_x: station wing_tip_x (front) | 5 | <= 40 | PASS |
| F1.station.wing_top_mid: station wing_top_mid (front) | 522.5 | <= 40 | **FAIL (hard)** |
| F1.station.main_wheel_x: station main_wheel_x (front) | 5 | <= 40 | PASS |
| F2.feature.window_frames: feature window_frames (geometry, required) | - | present 12 | **FAIL**: no probe box defined yet |
| F2.feature.window_seals: feature window_seals (geometry, optional) | - | present 12 | **FAIL**: no probe box defined yet |
| F2.feature.door_gaps: feature door_gaps (geometry, required) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.door_handles: feature door_handles (geometry, required) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.cowl_lip_and_inlets: feature cowl_lip_and_inlets (geometry, required) | - | present 3 | **FAIL**: no probe box defined yet |
| F2.feature.cowl_flap_outlines: feature cowl_flap_outlines (texture, optional) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.oil_door: feature oil_door (geometry, optional) | - | present 1 | **FAIL**: no probe box defined yet |
| F2.feature.panel_lines_fuselage: feature panel_lines_fuselage (texture, required) | - | present 20 | **FAIL**: no probe box defined yet |
| F2.feature.panel_lines_wing: feature panel_lines_wing (texture, required) | - | present 12 | **FAIL**: no probe box defined yet |
| F2.feature.rivet_rows: feature rivet_rows (texture, required) | - | present 30 | **FAIL**: no probe box defined yet |
| F2.feature.wing_root_fairings: feature wing_root_fairings (geometry, required) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.cabin_steps: feature cabin_steps (geometry, required) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.stall_warning_vent: feature stall_warning_vent (geometry, required) | - | present 1 | **FAIL**: no probe box defined yet |
| F2.feature.fuel_cap_and_vents: feature fuel_cap_and_vents (geometry, required) | - | present 4 | **FAIL**: no probe box defined yet |
| F2.feature.elevator_trim_tab: feature elevator_trim_tab (geometry, required) | - | present 1 | **FAIL**: no probe box defined yet |
| F2.feature.landing_light_lens: feature landing_light_lens (geometry, required) | - | present 1 | **FAIL**: no probe box defined yet |
| F2.feature.nav_strobe_lenses: feature nav_strobe_lenses (geometry, required) | - | present 6 | **FAIL**: no probe box defined yet |
| F2.feature.beacon: feature beacon (geometry, required) | - | present 1 | **FAIL**: no probe box defined yet |
| F2.feature.pitot_and_static: feature pitot_and_static (geometry, required) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.antennas: feature antennas (geometry, required) | - | present 3 | **FAIL**: no probe box defined yet |
| F2.feature.hinge_lines_control_surfaces: feature hinge_lines_control_surfaces (geometry, required) | - | present 8 | **FAIL**: no probe box defined yet |
| F2.feature.prop_tip_paint: feature prop_tip_paint (texture, required) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.prop_blade_decals_neutral: feature prop_blade_decals_neutral (texture, optional) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.spinner_backplate_and_screws: feature spinner_backplate_and_screws (geometry, required) | - | present 6 | **FAIL**: no probe box defined yet |
| F2.feature.cheat_line_two_colours: feature cheat_line_two_colours (texture, required) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.exhaust_stain: feature exhaust_stain (texture, optional) | - | present 1 | **FAIL**: no probe box defined yet |
| F2.feature.hinge_grime: feature hinge_grime (texture, optional) | - | present 4 | **FAIL**: no probe box defined yet |
| F2.feature.wheel_fairing_split_lines: feature wheel_fairing_split_lines (texture, required) | - | present 3 | **FAIL**: no probe box defined yet |
| F2.feature.brake_lines_and_hubs: feature brake_lines_and_hubs (geometry, optional) | - | present 3 | **FAIL**: no probe box defined yet |
| F2.feature.strut_fairings_and_attach_plates: feature strut_fairings_and_attach_plates (geometry, required) | - | present 4 | **FAIL**: no probe box defined yet |
| F2.feature.tie_down_rings: feature tie_down_rings (geometry, optional) | - | present 3 | **FAIL**: no probe box defined yet |
| F2.feature.gauge_faces_legible: feature gauge_faces_legible (texture, required) | - | present 6 | **FAIL**: no probe box defined yet |
| F2.feature.radio_stack_faces: feature radio_stack_faces (texture, required) | - | present 4 | **FAIL**: no probe box defined yet |
| F2.feature.placards_generic: feature placards_generic (texture, optional) | - | present 4 | **FAIL**: no probe box defined yet |
| F2.feature.seat_stitching_and_belts: feature seat_stitching_and_belts (texture, optional) | - | present 4 | **FAIL**: no probe box defined yet |
| F2.feature.yoke_buttons_and_grips: feature yoke_buttons_and_grips (geometry, optional) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.rudder_pedal_brakes: feature rudder_pedal_brakes (geometry, required) | - | present 4 | **FAIL**: no probe box defined yet |
| F2.feature.door_seals_and_latches_interior: feature door_seals_and_latches_interior (geometry, optional) | - | present 2 | **FAIL**: no probe box defined yet |
| F2.feature.tail_navigation_light: feature tail_navigation_light (geometry, required) | - | present 1 | **FAIL**: no probe box defined yet |
| F2.feature.static_wicks: feature static_wicks (geometry, optional) | - | present 4 | **FAIL**: no probe box defined yet |
| F2.feature.fin_dorsal_and_rudder_balance: feature fin_dorsal_and_rudder_balance (geometry, required) | - | present 1 | **FAIL**: no probe box defined yet |
| F2.required: required features present (0 of 28) | 0.00 % | >= 100.00 % | **FAIL (hard)** |
| F2.optional: optional features present (0 of 13) | 0.00 % | >= 75.00 % | **FAIL** |
| F3.cowl_lip.ratio: cowl_lip width/height | 1.061 | in [1.170, 1.430] | **FAIL (hard)** |
| F3.cowl_lip.fill: cowl_lip fill ratio | 0.869 | >= 0.760 | PASS |
| F3.cowl_lip.radius: cowl_lip tightest corner | 188.5 | in [15, 90] | **FAIL (hard)** |
| F3.firewall.ratio: firewall width/height | 0.983 | in [0.900, 1.050] | PASS |
| F3.firewall.fill: firewall fill ratio | 0.892 | >= 0.800 | PASS |
| F3.firewall.flat: firewall side flatness | 5.8 | <= 25 | PASS |
| F3.firewall.radius: firewall tightest corner | 172.9 | in [50, 250] | PASS |
| F3.cabin_door.ratio: cabin_door width/height | 0.690 | in [0.620, 0.720] | PASS |
| F3.cabin_door.fill: cabin_door fill ratio | 0.921 | >= 0.840 | PASS |
| F3.cabin_door.flat: cabin_door side flatness | 4.4 | <= 12 | PASS |
| F3.cabin_door.radius: cabin_door tightest corner | 180.1 | in [60, 250] | PASS |
| F3.cabin_rear.ratio: cabin_rear width/height | 0.713 | in [0.680, 0.800] | PASS |
| F3.cabin_rear.fill: cabin_rear fill ratio | 0.909 | >= 0.820 | PASS |
| F3.cabin_rear.flat: cabin_rear side flatness | 4.2 | <= 15 | PASS |
| F3.cabin_rear.radius: cabin_rear tightest corner | 186.8 | in [60, 250] | PASS |
| F3.tail_cone.ratio: tail_cone width/height | 0.862 | in [0.790, 0.970] | PASS |
| F3.tail_cone.fill: tail_cone fill ratio | 0.759 | >= 0.760 | **FAIL (hard)** |
| F3.tail_cone.radius: tail_cone tightest corner | 149.4 | in [80, 300] | PASS |
| F4.glb_bytes: file size (textured family) | 1.20 MB | <= 25.17 MB | PASS |
| F4.primitive_vertices: largest primitive (vertices) | 6 875 | <= 65 535 | PASS |
| F4.maps: visible primitives with base colour, normal and ORM maps | 0 | == 28 | **FAIL (hard)**: 28 of 28 lack them; the file has no textures |
| F4.uvs: visible primitives with TEXCOORD_0 | 0 | == 28 | **FAIL (hard)**: 28 of 28 have no UVs |
| F4.colour_double_tint: textured primitives that also carry COLOR_0 | 0 | == 0 | PASS |
| F4.sizes: texture sizes power of two and within the limit | 0 | == 0 | **N.A. (hard)**: no textures |
| F4.gpu_bytes: uncompressed RGBA8 with mips | 0.00 MB | <= 268.44 MB | PASS |
| F4.spread: texel density spread | - | - | **N.A. (hard)**: the model has no UVs |
| F4.uv_range: UVs inside 0..1 | - | - | **N.A. (hard)**: the model has no UVs |
| F4.overlap: UV overlap | - | - | **N.A. (hard)**: the model has no UVs |
| F4.padding: island padding | - | - | **N.A. (hard)**: the model has no UVs |
| F4.density.small_parts: texel density p5, small_parts | - | >= 250 | **N.A. (hard)**: the model has no UVs |
| F4.density.panel_faces: texel density p5, panel_faces | - | >= 1 500 | **N.A. (hard)**: the model has no UVs |
| F4.density.airframe: texel density p5, airframe | - | >= 400 | **N.A. (hard)**: the model has no UVs |
| F4.normal_length: normal vector length | - | - | **N.A. (hard)**: no decodable textures |
| F4.metallic_binary: metallic channel binary | - | - | **N.A. (hard)**: no decodable textures |
| F4.base_colour_range: base colour range | - | - | **N.A. (hard)**: no decodable textures |
| F4.bleed: edge bleed | - | - | **N.A. (hard)**: no decodable textures |
| F5.wing_trailing_edge.radius: wing_trailing_edge edge radius (worst of 2) | 0 | in [1, 3] | **FAIL (hard)**: 2 probe(s) with fewer than 3 mesh segments |
| F5.wing_trailing_edge.thickness: wing_trailing_edge edge thickness (worst) | 0.3 | in [1.5, 5] | **FAIL (hard)** |
| F5.control_surface_edges.radius: control_surface_edges edge radius (worst of 2) | 0 | in [2, 6] | **FAIL (hard)**: 2 probe(s) with fewer than 3 mesh segments |
| F5.door_frame.radius: door_frame edge radius (worst of 1) | 0 | in [2, 6] | **FAIL (hard)**: 1 probe(s) with fewer than 3 mesh segments |
| F5.window_frames.radius: window_frames edge radius | - | in [3, 8] | **FAIL (hard)**: no probe defined yet |
| F5.cowl_lip.radius: cowl_lip edge radius (worst of 1) | 0 | in [8, 20] | **FAIL (hard)**: 1 probe(s) with fewer than 3 mesh segments |
| F5.tailplane_and_fin_tips.radius: tailplane_and_fin_tips edge radius (worst of 1) | 0 | in [3, 10] | **FAIL (hard)**: 1 probe(s) with fewer than 3 mesh segments |
| F5.fin_leading_edge.radius: fin_leading_edge edge radius (worst of 1) | 4.24 | in [4, 15] | PASS |
| F5.max_radius: largest measured named-edge radius | 4.24 | <= 40 | PASS |
| F5.hard_creases: share of edge length with a >30 degree crease | 4.66 % | in [3.00 %, 8.00 %] | PASS |

## F1 detail

| View | IoU | model->ref p95 / max (mm) | ref->model p95 / max (mm) | ref scale (mm/px h, v) |
|---|---|---|---|---|
| side | 0.7015 | 534.2 / 753.3 | 349.1 / 494.8 | 5.69, 4.99 |
| top | 0.7393 | 480.0 / 620.0 | 559.8 / 667.5 | 5.70, 6.00 |
| front | 0.2551 | 535.0 / 550.1 | 555.0 / 585.8 | 5.77, 4.77 |

| Station | View | Model (m) | Reference (m) | Delta (mm) |
|---|---|---|---|---|
| spinner_tip | side | -2.53 | -2.53 | 0.0 |
| rudder_te | side | 5.75 | 5.75 | 0.0 |
| fin_top | side | 1.52 | 1.52 | 0.0 |
| belly_line | side | -0.4021 | -0.4629 | 60.7 |
| cowl_front | side | -2.24 | -2.1229 | -117.1 |
| cowl_top | side | 0.435 | 0.255 | 180.0 |
| windscreen_base | side | -1.4607 | -0.8979 | -562.9 |
| cabin_roof | side | 0.995 | 0.7921 | 202.9 |
| fin_leading_edge | side | 1.9693 | 4.8093 | -2840.0 |
| nose_wheel_centre | side | -1.8 | -1.4225 | -377.5 |
| main_wheel_centre | side | 0.48 | 0.215 | 265.0 |
| wing_le_root | top | -0.45 | -0.685 | 235.0 |
| wing_le_tip | top | -0.0189 | -0.66 | 641.1 |
| wing_te_root | top | 1.18 | 1.205 | -25.0 |
| wing_te_tip | top | 1.18 | 0.6682 | 511.8 |
| tailplane_tip | top | 1.725 | 1.64 | 85.0 |
| tailplane_te | top | 5.3661 | 5.1732 | 192.9 |
| wing_tip_x | front | 5.505 | 5.5 | 5.0 |
| wing_top_mid | front | 1.205 | 0.6825 | 522.5 |
| main_wheel_x | front | 1.27 | 1.275 | -5.0 |

## F3 sections

| Section | Width (m) | Height (m) | Ratio | Fill | Flat (mm) | Min radius (mm) |
|---|---|---|---|---|---|---|
| cowl_lip | 0.8563 | 0.8074 | 1.0607 | 0.8686 | - | 188.5 |
| firewall | 1.0169 | 1.0345 | 0.983 | 0.8916 | 5.8 | 172.9 |
| cabin_door | 1.12 | 1.6225 | 0.6903 | 0.9208 | 4.4 | 180.1 |
| cabin_rear | 1.0798 | 1.5147 | 0.7129 | 0.9085 | 4.2 | 186.8 |
| tail_cone | 0.6493 | 0.7532 | 0.8621 | 0.7588 | - | 149.4 |

## F4 sheets

No UV sheets (the model has no UVs, or the vertex-colour family was audited).
