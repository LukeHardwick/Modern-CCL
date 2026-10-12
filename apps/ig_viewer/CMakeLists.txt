# Visual mock IG: raylib + Dear ImGui + rlImGui (fetched only when CIGI_BUILD_VIEWER=ON, see
# cmake/Dependencies.cmake). Thin rendering/UI code over the headless cigi_mock_ig library.
add_executable(ig_viewer
    app.cpp
    args.cpp
    main.cpp
    renderer.cpp
    ui.cpp)
target_link_libraries(ig_viewer PRIVATE cigi::mock_ig cigi_imgui raylib)
target_compile_features(ig_viewer PRIVATE cxx_std_23)
cigi_apply_project_options(ig_viewer)
