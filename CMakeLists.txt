cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

project(FlowStateSuite VERSION 1.3.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pass -DMAX_VERSION=2027 to target a different Max install.
if(NOT DEFINED MAX_VERSION)
    set(MAX_VERSION "2026" CACHE STRING "Target 3ds Max version")
endif()

if(NOT DEFINED MAXSDK_PATH)
    set(MAXSDK_PATH "C:/Program Files/Autodesk/3ds Max ${MAX_VERSION} SDK/maxsdk"
        CACHE PATH "Path to 3ds Max SDK")
endif()

if(NOT EXISTS "${MAXSDK_PATH}/include/max.h")
    message(FATAL_ERROR "Cannot find max.h at ${MAXSDK_PATH}/include/max.h. Set -DMAXSDK_PATH=...")
endif()

if(NOT DEFINED MAX_PLUGINS_DIR)
    set(MAX_PLUGINS_DIR "C:/Program Files/Autodesk/3ds Max ${MAX_VERSION}/plugins"
        CACHE PATH "3ds Max plugins directory")
endif()

message(STATUS "Building FlowState plugin suite for 3ds Max ${MAX_VERSION}")
message(STATUS "  SDK: ${MAXSDK_PATH}")

set(MAXSDK_LIB_DIR "${MAXSDK_PATH}/lib/x64/Release")

function(configure_max_plugin target_name output_name plugin_type)
    set_target_properties(${target_name} PROPERTIES
        SUFFIX ".${plugin_type}"
        OUTPUT_NAME "${output_name}"
    )

    target_include_directories(${target_name} PRIVATE
        "${MAXSDK_PATH}/include"
    )

    target_compile_definitions(${target_name} PRIVATE
        NOMINMAX
        _UNICODE UNICODE
        WIN32 _WIN32
        _USRDLL
        _CRT_SECURE_NO_WARNINGS
    )

    target_link_directories(${target_name} PRIVATE
        "${MAXSDK_LIB_DIR}"
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W3 /MP)
        set_property(TARGET ${target_name} PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()

    install(TARGETS ${target_name}
        RUNTIME DESTINATION "${MAX_PLUGINS_DIR}"
        LIBRARY DESTINATION "${MAX_PLUGINS_DIR}"
    )
endfunction()

# One native collection, one binary: every class below ships in FlowState.gup.
set(FLOWSTATE_CORE_SOURCES
    src/flowstate.cpp
    src/FlowState.def
)

set(FLOWSTATE_UI_SOURCES
    src/powershader.cpp
    src/powershader.h
    src/modstack.cpp
    src/modstack.h
)

set(FLOWSTATE_MODELING_SOURCES
    src/normalize_edges/f2_extend_tool.cpp
    src/normalize_edges/smooth_bridge_tool.cpp
)

set(FLOWSTATE_NORMALIZE_POLY_SOURCES
    src/normalize_edges/normalize_poly.cpp
    src/normalize_edges/normalize_poly.h
    src/normalize_edges/normalize_poly.rc
    src/normalize_edges/resource.h
)

set(FLOWSTATE_LOOP_SUBDIVISION_SOURCES
    src/modifiers/loop_subdivision/loop_subdivision.cpp
    src/modifiers/loop_subdivision/loop_subdivision.h
    src/modifiers/loop_subdivision/loop_subdivision.rc
    src/modifiers/loop_subdivision/loop_subdivision_resource.h
)

add_library(FlowState SHARED
    ${FLOWSTATE_CORE_SOURCES}
    ${FLOWSTATE_UI_SOURCES}
    ${FLOWSTATE_MODELING_SOURCES}
    ${FLOWSTATE_NORMALIZE_POLY_SOURCES}
    ${FLOWSTATE_LOOP_SUBDIVISION_SOURCES}
)

source_group("Core" FILES ${FLOWSTATE_CORE_SOURCES})
source_group("UI" FILES ${FLOWSTATE_UI_SOURCES})
source_group("Modeling" FILES ${FLOWSTATE_MODELING_SOURCES})
source_group("Modifiers\\Normalize Poly" FILES ${FLOWSTATE_NORMALIZE_POLY_SOURCES})
source_group("Modifiers\\Loop Subdivision" FILES ${FLOWSTATE_LOOP_SUBDIVISION_SOURCES})

configure_max_plugin(FlowState FlowState gup)
target_include_directories(FlowState PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/normalize_edges"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/modifiers/loop_subdivision"
)
target_link_libraries(FlowState PRIVATE
    core.lib
    maxutil.lib
    gup.lib
    Paramblk2.lib
    comctl32.lib
    Geom.lib
    Maxscrpt.lib
    mesh.lib
    MNMath.lib
    poly.lib
    bmm.lib
    gdiplus.lib
    uiautomationcore.lib
)
if(MSVC)
    target_compile_options(FlowState PRIVATE /EHa /O2 /fp:fast /openmp)
endif()

# Keep the two release deliverables together in the selected configuration
# directory (for example build/Release/).
add_custom_command(TARGET FlowState POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/macros/flowstate_config.ms"
        "$<TARGET_FILE_DIR:FlowState>/flowstate_config.ms"
    VERBATIM
)
