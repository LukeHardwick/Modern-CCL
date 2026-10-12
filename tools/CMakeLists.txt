# Developer tools shared by the demo apps, the Wireshark verification and the benchmarks.
add_library(cigi_tools STATIC src/pcap_writer.cpp)
add_library(cigi::tools ALIAS cigi_tools)
target_sources(cigi_tools PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/cigi/tools/pcap_writer.hpp)
target_compile_features(cigi_tools PUBLIC cxx_std_23)
target_link_libraries(cigi_tools PUBLIC cigi::cigi)
set_target_properties(cigi_tools PROPERTIES POSITION_INDEPENDENT_CODE ON)
cigi_apply_project_options(cigi_tools)
