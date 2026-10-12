add_executable(demo_host main.cpp)
target_link_libraries(demo_host PRIVATE cigi::demo_common)
cigi_apply_project_options(demo_host)
