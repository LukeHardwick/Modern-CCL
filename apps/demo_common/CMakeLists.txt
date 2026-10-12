# Shared pieces of the demo apps (and the viewer): CLI options, the pcap-recording UDP link,
# the headless mock IG and the demo Host. Unlike the library these use exceptions (CLI11).
add_library(cigi_demo_common STATIC
    demo_host.cpp
    demo_ig.cpp
    options.cpp
    support.cpp
    udp_link.cpp)
add_library(cigi::demo_common ALIAS cigi_demo_common)
# Headers are included as "demo_common/<name>.hpp" (this directory's parent is the include root).
target_include_directories(cigi_demo_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(cigi_demo_common PUBLIC cxx_std_23)
target_link_libraries(cigi_demo_common
    PUBLIC cigi::runtime cigi::mock_ig cigi::tools cigi::transport CLI11::CLI11)
set_target_properties(cigi_demo_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
cigi_apply_project_options(cigi_demo_common)
