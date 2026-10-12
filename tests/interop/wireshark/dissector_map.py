"""Mapping from this library's packet and field names to Wireshark's CIGI dissector field names.

Data only; check_tshark.py applies it. To cover a new packet, append one entry to PACKETS (Phase B
tasks add rows here). Field names on the left are the ones make_wireshark_corpus.cpp writes to
expected.json (the C++ struct member names).

A field is (our_name, dissector_name, kind) or (our_name, dissector_name, kind, known_issue):

  kind  meaning
  ----  ------------------------------------------------------------------------------------
  int   integer or enum value; tshark prints the raw number
  bool  one bit; tshark prints 0 or 1
  f32   IEEE single, tshark's text rounded to single must equal the encoded value
  f64   IEEE double, tshark's text must be within 1 ulp of the encoded value
  word  a 32-bit user-defined word that the dissector shows as raw bytes, colon separated, in
        wire order; compared against the value written in the message's wire byte order
  magic the Byte Swap Magic Number: "0x0080" for a little-endian sender, "0x8000" for big-endian

  known_issue is a dict {"note": str, "wireshark": <value>} for a field where the dissector is
  wrong according to the ICD: the check then requires the dissector's (wrong) value `wireshark`
  instead, so the day Wireshark is fixed the test fails and the entry can be dropped. Every entry
  is described in docs/interop/discrepancies.md.
"""

# Dissector tree name -> (our packet type, opcode, size in bytes).
PACKET_TREES = {
    "cigi.ig_control": ("ig_control", 1, 24),
    "cigi.entity_control": ("entity_control", 2, 48),
    "cigi.component_control": ("component_control", 4, 32),
    "cigi.art_part_control": ("articulated_part_control", 6, 32),
    "cigi.rate_control": ("rate_control", 8, 32),
    "cigi.celestial_sphere_control": ("celestial_sphere_control", 9, 16),
    "cigi.view_control": ("view_control", 16, 32),
    "cigi.view_def": ("view_definition", 21, 32),
    "cigi.hat_hot_request": ("hat_hot_request", 24, 32),
    "cigi.sof": ("start_of_frame", 101, 24),
    "cigi.hat_hot_response": ("hat_hot_response", 102, 16),
    "cigi.hat_hot_ext_response": ("hat_hot_extended_response", 103, 40),
}

PACKETS = {
    "ig_control": [
        ("minor_version", "cigi.ig_control.minor_version", "int"),
        ("database_number", "cigi.ig_control.db_number", "int"),
        ("ig_mode", "cigi.ig_control.ig_mode", "int"),
        ("timestamp_valid", "cigi.ig_control.timestamp_valid", "bool"),
        ("extrapolation_enabled", "cigi.ig_control.extrapolation_enable", "bool"),
        ("host_frame_number", "cigi.ig_control.host_frame_number", "int"),
        # Ticks of 10 us (ICD 4.1.1); the dissector shows the same number but labels it
        # "microseconds", which is a label issue only (values agree).
        ("timestamp", "cigi.ig_control.timestamp", "int"),
        ("last_ig_frame_number", "cigi.ig_control.last_ig_frame_number", "int"),
        (None, "cigi.byte_swap", "magic"),
    ],
    "start_of_frame": [
        ("minor_version", "cigi.sof.minor_version", "int"),
        ("database_number", "cigi.sof.db_number", "int"),
        ("ig_status", "cigi.sof.ig_status", "int"),
        ("ig_mode", "cigi.sof.ig_mode", "int"),
        ("timestamp_valid", "cigi.sof.timestamp_valid", "bool"),
        ("earth_reference_model", "cigi.sof.earth_reference_model", "int"),
        ("ig_frame_number", "cigi.sof.ig_frame_number", "int"),
        ("timestamp", "cigi.sof.timestamp", "int"),
        ("last_host_frame_number", "cigi.sof.last_host_frame_number", "int"),
        (None, "cigi.byte_swap", "magic"),
    ],
    "entity_control": [
        ("entity_id", "cigi.entity_control.entity_id", "int"),
        ("entity_state", "cigi.entity_control.entity_state", "int"),
        ("attach_state", "cigi.entity_control.attach_state", "int"),
        ("collision_detection_enabled", "cigi.entity_control.coll_det_request", "bool"),
        ("inherit_alpha", "cigi.entity_control.inherit_alpha", "bool"),
        ("ground_clamp", "cigi.entity_control.ground_ocean_clamp", "int"),
        ("animation_direction", "cigi.entity_control.animation_dir", "int"),
        ("animation_loop_mode", "cigi.entity_control.animation_loop_mode", "int"),
        ("animation_state", "cigi.entity_control.animation_state", "int"),
        ("extrapolation_enabled", "cigi.entity_control.extrapolation_enable", "bool"),
        ("alpha", "cigi.entity_control.alpha", "int"),
        ("entity_type", "cigi.entity_control.entity_type", "int"),
        ("parent_id", "cigi.entity_control.parent_id", "int"),
        ("roll", "cigi.entity_control.roll", "f32"),
        ("pitch", "cigi.entity_control.pitch", "f32"),
        ("yaw", "cigi.entity_control.yaw", "f32"),
        ("latitude_or_x", "cigi.entity_control.lat_xoff", "f64"),
        ("longitude_or_y", "cigi.entity_control.lon_yoff", "f64"),
        ("altitude_or_z", "cigi.entity_control.alt_zoff", "f64"),
    ],
    "articulated_part_control": [
        ("entity_id", "cigi.art_part_control.entity_id", "int"),
        ("articulated_part_id", "cigi.art_part_control.part_id", "int"),
        ("part_enabled", "cigi.art_part_control.part_enable", "bool"),
        ("x_offset_enabled", "cigi.art_part_control.xoff_enable", "bool"),
        ("y_offset_enabled", "cigi.art_part_control.yoff_enable", "bool"),
        ("z_offset_enabled", "cigi.art_part_control.zoff_enable", "bool"),
        ("roll_enabled", "cigi.art_part_control.roll_enable", "bool"),
        ("pitch_enabled", "cigi.art_part_control.pitch_enable", "bool"),
        ("yaw_enabled", "cigi.art_part_control.yaw_enable", "bool"),
        ("x_offset", "cigi.art_part_control.xoff", "f32"),
        ("y_offset", "cigi.art_part_control.yoff", "f32"),
        ("z_offset", "cigi.art_part_control.zoff", "f32"),
        ("roll", "cigi.art_part_control.roll", "f32"),
        ("pitch", "cigi.art_part_control.pitch", "f32"),
        ("yaw", "cigi.art_part_control.yaw", "f32"),
    ],
    "rate_control": [
        ("entity_id", "cigi.rate_control.entity_id", "int"),
        ("articulated_part_id", "cigi.rate_control.part_id", "int"),
        ("apply_to_articulated_part", "cigi.rate_control.apply_to_part", "bool"),
        ("coordinate_system", "cigi.rate_control.coordinate_system", "int"),
        ("x_linear_rate", "cigi.rate_control.x_rate", "f32"),
        ("y_linear_rate", "cigi.rate_control.y_rate", "f32"),
        ("z_linear_rate", "cigi.rate_control.z_rate", "f32"),
        ("roll_rate", "cigi.rate_control.roll_rate", "f32"),
        ("pitch_rate", "cigi.rate_control.pitch_rate", "f32"),
        ("yaw_rate", "cigi.rate_control.yaw_rate", "f32"),
    ],
    "component_control": [
        ("component_id", "cigi.component_control.component_id", "int"),
        ("instance_id", "cigi.component_control.instance_id", "int"),
        ("component_class", "cigi.component_control.component_class", "int"),
        ("component_state", "cigi.component_control.component_state", "int"),
        ("data_1", "cigi.component_control.data_1", "word"),
        ("data_2", "cigi.component_control.data_2", "word"),
        ("data_3", "cigi.component_control.data_3", "word"),
        ("data_4", "cigi.component_control.data_4", "word"),
        ("data_5", "cigi.component_control.data_5", "word"),
        ("data_6", "cigi.component_control.data_6", "word"),
    ],
    "celestial_sphere_control": [
        ("hour", "cigi.celestial_sphere_control.hour", "int"),
        ("minute", "cigi.celestial_sphere_control.minute", "int"),
        ("ephemeris_model_enabled", "cigi.celestial_sphere_control.ephemeris_enable", "bool"),
        ("sun_enabled", "cigi.celestial_sphere_control.sun_enable", "bool"),
        ("moon_enabled", "cigi.celestial_sphere_control.moon_enable", "bool"),
        ("star_field_enabled", "cigi.celestial_sphere_control.star_enable", "bool"),
        ("date_time_valid", "cigi.celestial_sphere_control.date_time_valid", "bool"),
        ("date", "cigi.celestial_sphere_control.date", "int"),
        ("star_field_intensity", "cigi.celestial_sphere_control.star_intensity", "f32"),
    ],
    "view_control": [
        ("view_id", "cigi.view_control.view_id", "int"),
        ("group_id", "cigi.view_control.group_id", "int"),
        ("x_offset_enabled", "cigi.view_control.xoff_enable", "bool"),
        ("y_offset_enabled", "cigi.view_control.yoff_enable", "bool"),
        ("z_offset_enabled", "cigi.view_control.zoff_enable", "bool"),
        ("roll_enabled", "cigi.view_control.roll_enable", "bool"),
        ("pitch_enabled", "cigi.view_control.pitch_enable", "bool"),
        ("yaw_enabled", "cigi.view_control.yaw_enable", "bool"),
        ("entity_id", "cigi.view_control.entity_id", "int"),
        ("x_offset", "cigi.view_control.xoff", "f32"),
        ("y_offset", "cigi.view_control.yoff", "f32"),
        ("z_offset", "cigi.view_control.zoff", "f32"),
        ("roll", "cigi.view_control.roll", "f32"),
        ("pitch", "cigi.view_control.pitch", "f32"),
        ("yaw", "cigi.view_control.yaw", "f32"),
    ],
    "view_definition": [
        ("view_id", "cigi.view_def.view_id", "int"),
        ("group_id", "cigi.view_def.group_id", "int"),
        ("near_enabled", "cigi.view_def.near_enable", "bool"),
        ("far_enabled", "cigi.view_def.far_enable", "bool"),
        ("left_enabled", "cigi.view_def.left_enable", "bool"),
        ("right_enabled", "cigi.view_def.right_enable", "bool"),
        ("top_enabled", "cigi.view_def.top_enable", "bool"),
        ("bottom_enabled", "cigi.view_def.bottom_enable", "bool"),
        ("mirror_mode", "cigi.view_def.mirror_mode", "int"),
        ("pixel_replication_mode", "cigi.view_def.pixel_replication", "int"),
        ("projection_type", "cigi.view_def.projection_type", "int"),
        ("reorder", "cigi.view_def.reorder", "bool"),
        ("view_type", "cigi.view_def.view_type", "int"),
        ("near_plane", "cigi.view_def.near", "f32"),
        ("far_plane", "cigi.view_def.far", "f32"),
        ("left", "cigi.view_def.left", "f32"),
        ("right", "cigi.view_def.right", "f32"),
        ("top", "cigi.view_def.top", "f32"),
        ("bottom", "cigi.view_def.bottom", "f32"),
    ],
    "hat_hot_request": [
        ("hat_hot_id", "cigi.hat_hot_request.hat_hot_id", "int"),
        ("request_type", "cigi.hat_hot_request.type", "int"),
        ("coordinate_system", "cigi.hat_hot_request.coordinate_system", "int"),
        ("update_period", "cigi.hat_hot_request.update_period", "int"),
        ("entity_id", "cigi.hat_hot_request.entity_id", "int"),
        ("latitude_or_x", "cigi.hat_hot_request.lat_xoff", "f64"),
        ("longitude_or_y", "cigi.hat_hot_request.lon_yoff", "f64"),
        ("altitude_or_z", "cigi.hat_hot_request.alt_zoff", "f64"),
    ],
    "hat_hot_response": [
        ("hat_hot_id", "cigi.hat_hot_response.hat_hot_id", "int"),
        ("valid", "cigi.hat_hot_response.valid", "bool"),
        ("response_type", "cigi.hat_hot_response.type", "int"),
        ("host_frame_lsn", "cigi.hat_hot_response.host_frame_number_lsn", "int"),
        ("height", "cigi.hat_hot_response.height", "f64"),
    ],
    "hat_hot_extended_response": [
        ("hat_hot_id", "cigi.hat_hot_ext_response.hat_hot_id", "int"),
        ("valid", "cigi.hat_hot_ext_response.valid", "bool"),
        ("host_frame_lsn", "cigi.hat_hot_ext_response.host_frame_number_lsn", "int"),
        ("hat", "cigi.hat_hot_ext_response.hat", "f64"),
        ("hot", "cigi.hat_hot_ext_response.hot", "f64"),
        ("material_code", "cigi.hat_hot_ext_response.material_code", "int"),
        ("normal_azimuth", "cigi.hat_hot_ext_response.normal_vector_azimuth", "f32"),
        ("normal_elevation", "cigi.hat_hot_ext_response.normal_vector_elevation", "f32"),
    ],
}
