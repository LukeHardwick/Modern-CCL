# cigi_db: headless validator / lister of mock-IG databases (T20). The report is machine-written
# JSON (glaze, built without exceptions); the command logic lives in a library so it is unit
# tested like the rest.
add_library(cigi_db_lib STATIC
    cli.cpp
    report.cpp)
target_include_directories(cigi_db_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(cigi_db_lib PUBLIC cxx_std_23)
target_link_libraries(cigi_db_lib PUBLIC cigi::mock_ig PRIVATE glaze::glaze)
if(MSVC)
    target_compile_options(cigi_db_lib PRIVATE /EHs-c-)
    target_compile_definitions(cigi_db_lib PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(cigi_db_lib PRIVATE -fno-exceptions)
endif()
cigi_apply_project_options(cigi_db_lib)

add_executable(cigi_db main.cpp)
target_link_libraries(cigi_db PRIVATE cigi_db_lib)
target_compile_features(cigi_db PRIVATE cxx_std_23)
cigi_apply_project_options(cigi_db)
