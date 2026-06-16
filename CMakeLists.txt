cmake_minimum_required(VERSION 3.12)
project(habitat-ugens LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT DEFINED SC_PATH)
    message(FATAL_ERROR
        "Pass -DSC_PATH=/path/to/supercollider source tree, MATCHING the SC you run.")
endif()

include_directories(
    "${SC_PATH}/include/plugin_interface"
    "${SC_PATH}/include/common"
    "${SC_PATH}/include/server"
)

# ---- the only thing per-unit briefs touch: append the unit name here ----
set(HABITAT_UGENS
    Tomograph
    # Colmatage
)

foreach(ugen ${HABITAT_UGENS})
    add_library(${ugen} MODULE "src/${ugen}.cpp")
    set_target_properties(${ugen} PROPERTIES
        PREFIX ""
        SUFFIX ".scx"
        CXX_VISIBILITY_PRESET hidden)
    if(APPLE)
        set_target_properties(${ugen} PROPERTIES
            LINK_FLAGS "-bundle -undefined dynamic_lookup")
    endif()
    install(TARGETS ${ugen} DESTINATION ".")
    install(FILES "sc/${ugen}.sc" DESTINATION ".")
endforeach()
