# Applied globally (directory scope) so third-party code (GoogleTest) is instrumented too.
if(CIGI_ENABLE_SANITIZERS)
    if(MSVC)
        # LIMITATION: MSVC supports AddressSanitizer only; there is no UBSan or LSan.
        add_compile_options(/fsanitize=address)
        # /fsanitize=address is incompatible with /RTC and incremental linking.
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        add_link_options(/INCREMENTAL:NO)
    else()
        set(_cigi_san -fsanitize=address,undefined -fno-omit-frame-pointer
                      -fno-sanitize-recover=undefined)
        add_compile_options(${_cigi_san})
        add_link_options(${_cigi_san})
    endif()
endif()
