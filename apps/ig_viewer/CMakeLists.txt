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

if(APPLE)
    # PLATFORM: raylib needs these system frameworks (OpenGL is deprecated on macOS but still
    # what raylib uses; deprecation warnings come from third-party headers only).
    target_link_libraries(ig_viewer PRIVATE "-framework Cocoa" "-framework IOKit"
                          "-framework CoreVideo" "-framework OpenGL")
endif()
