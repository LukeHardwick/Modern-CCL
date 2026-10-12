# Headless mock IG: scene model, HAT/HOT responder, synthetic terrain, scripted host. No graphics
# dependencies; shared by the viewer (apps/ig_viewer) and the headless demo IG.
add_library(cigi_mock_ig STATIC
    describe.cpp
    geo.cpp
    hat_hot.cpp
    mock_ig.cpp
    scene.cpp
    scripted_host.cpp
    sky.cpp
    terrain.cpp)
add_library(cigi::mock_ig ALIAS cigi_mock_ig)
# Headers are included as "mock_ig/<name>.hpp" (this directory's parent is the include root).
target_include_directories(cigi_mock_ig PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(cigi_mock_ig PUBLIC cxx_std_23)
target_link_libraries(cigi_mock_ig PUBLIC cigi::cigi)
set_target_properties(cigi_mock_ig PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Like the library, the mock IG is written without exceptions.
if(MSVC)
    target_compile_options(cigi_mock_ig PRIVATE /EHs-c-)
    target_compile_definitions(cigi_mock_ig PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(cigi_mock_ig PRIVATE -fno-exceptions)
endif()
cigi_apply_project_options(cigi_mock_ig)
