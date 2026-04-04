cmake_minimum_required(VERSION 3.20)

# ╔══════════════════════════════════════════════════════════════╗
# ║  CHANGE THESE for your plugin                               ║
# ╚══════════════════════════════════════════════════════════════╝
set(PLUGIN_NAME "FlowState")             # Output filename (FlowState.gup)
set(PLUGIN_TYPE "gup")              # gup, dlo, dlm, dlt, dlr, dlu, dle, bmi

project(${PLUGIN_NAME} VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Max SDK ──────────────────────────────────────────────────────
if(NOT DEFINED MAXSDK_PATH)
    set(MAXSDK_PATH "C:/Program Files/Autodesk/3ds Max 2026 SDK/maxsdk"
        CACHE PATH "Path to 3ds Max SDK")
endif()

if(NOT EXISTS "${MAXSDK_PATH}/include/max.h")
    message(FATAL_ERROR "Cannot find max.h at ${MAXSDK_PATH}/include/max.h. Set -DMAXSDK_PATH=...")
endif()

# ── Sources (add your .cpp files here) ───────────────────────────
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_library(${PLUGIN_NAME} SHARED ${SOURCES})

# ── Output as plugin extension ───────────────────────────────────
set_target_properties(${PLUGIN_NAME} PROPERTIES
    SUFFIX ".${PLUGIN_TYPE}"
    OUTPUT_NAME "${PLUGIN_NAME}"
)

# Module definition — exports LibVersion, LibDescription, etc.
target_sources(${PLUGIN_NAME} PRIVATE "src/FlowState.def")

# ── Includes ─────────────────────────────────────────────────────
target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${MAXSDK_PATH}/include"
)

# ── Preprocessor ─────────────────────────────────────────────────
target_compile_definitions(${PLUGIN_NAME} PRIVATE
    NOMINMAX
    _UNICODE UNICODE
    WIN32 _WIN32
    _USRDLL
    _CRT_SECURE_NO_WARNINGS
)

# ── Link Max SDK libs ────────────────────────────────────────────
# Common libs — uncomment what you need:
#   core.lib        — always needed
#   maxutil.lib     — utilities, Class_ID, etc.
#   gup.lib         — GUP plugins
#   Geom.lib        — Point3, Matrix3, geometry math
#   Paramblk2.lib   — ClassDesc2, ParamBlockDesc2
#   Maxscrpt.lib    — MAXScript integration (mprintf, ExecuteMAXScriptScript)
#   mesh.lib        — Mesh, MNMesh operations
#   MNMath.lib      — MNMesh math
#   poly.lib        — Editable Poly
#   bmm.lib         — Bitmap Manager (render/viewport capture)

target_link_directories(${PLUGIN_NAME} PRIVATE
    "${MAXSDK_PATH}/lib/x64/Release"
)

target_link_libraries(${PLUGIN_NAME} PRIVATE
    core.lib
    maxutil.lib
    gup.lib
    Paramblk2.lib
    comctl32.lib
    # Geom.lib
    Maxscrpt.lib
    # mesh.lib
    bmm.lib
    gdiplus.lib
)

# ── MSVC settings ────────────────────────────────────────────────
if(MSVC)
    target_compile_options(${PLUGIN_NAME} PRIVATE /W3 /MP /EHa)
    set_property(TARGET ${PLUGIN_NAME} PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ── Deploy to Max ────────────────────────────────────────────────
set(MAX_PLUGINS_DIR "C:/Program Files/Autodesk/3ds Max 2026/plugins"
    CACHE PATH "3ds Max plugins directory")

install(TARGETS ${PLUGIN_NAME}
    RUNTIME DESTINATION "${MAX_PLUGINS_DIR}"
    LIBRARY DESTINATION "${MAX_PLUGINS_DIR}"
)
