add_executable(demo_ig main.cpp)
target_link_libraries(demo_ig PRIVATE cigi::demo_common)
cigi_apply_project_options(demo_ig)
