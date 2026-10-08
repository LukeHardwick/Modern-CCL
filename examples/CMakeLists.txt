add_executable(minimal_roundtrip minimal_roundtrip.cpp)
target_link_libraries(minimal_roundtrip PRIVATE cigi::cigi)
cigi_apply_project_options(minimal_roundtrip)

add_executable(cigi_frames wireshark/cigi_frames.cpp)
target_link_libraries(cigi_frames PRIVATE cigi::cigi)
cigi_apply_project_options(cigi_frames)
