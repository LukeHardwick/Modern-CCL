# Warning flags are carried by an INTERFACE target applied only to our own targets,
# never to third-party code.
add_library(cigi_warnings INTERFACE)
if(MSVC)
    target_compile_options(cigi_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8)
    if(CIGI_WARNINGS_AS_ERRORS)
        target_compile_options(cigi_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(cigi_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
        -Wcast-align -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference
        -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)
    if(CIGI_WARNINGS_AS_ERRORS)
        target_compile_options(cigi_warnings INTERFACE -Werror)
    endif()
endif()
