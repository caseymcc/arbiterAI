# The server's web dashboard (src/dashboard): a gweni app built by two
# ExternalProjects of that directory, each with its own vcpkg manifest and
# installed tree:
#
#   dashboard_web     wasm32-emscripten, always Release (the release workflows
#                     ship the debug build's server, and a debug wasm is many
#                     times larger); installs app.js/app.wasm/app.data and the
#                     shell into ${ARBITERAI_DASHBOARD_ASSET_DIR}
#   dashboard_native  x64-linux, the outer build type; builds dashboard_tests
#                     (${ARBITERAI_DASHBOARD_TESTS})
#
# ARBITERAI_WEB_DASHBOARD defaults ON when EMSDK names an emsdk (the image sets
# it); ON without one is a configure error. OFF builds neither, and the server
# serves a "dashboard not built" page.

set(ARBITERAI_EMSCRIPTEN_TOOLCHAIN "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
if(DEFINED ENV{EMSDK} AND EXISTS "${ARBITERAI_EMSCRIPTEN_TOOLCHAIN}")
    set(ARBITERAI_HAVE_EMSDK ON)
else()
    set(ARBITERAI_HAVE_EMSDK OFF)
endif()

option(ARBITERAI_WEB_DASHBOARD "Build the gweni web dashboard (needs emsdk: EMSDK)" ${ARBITERAI_HAVE_EMSDK})

if(NOT ARBITERAI_WEB_DASHBOARD)
    message(STATUS "Web dashboard: OFF (the server serves a \"dashboard not built\" page)")
    return()
endif()

if(NOT ARBITERAI_HAVE_EMSDK)
    if(DEFINED ENV{EMSDK})
        set(ARBITERAI_EMSDK_PROBLEM "EMSDK=$ENV{EMSDK} has no ${ARBITERAI_EMSCRIPTEN_TOOLCHAIN}")
    else()
        set(ARBITERAI_EMSDK_PROBLEM "EMSDK is not set")
    endif()
    message(FATAL_ERROR
        "ARBITERAI_WEB_DASHBOARD is ON but no emsdk was found (${ARBITERAI_EMSDK_PROBLEM}): "
        "set EMSDK to an emsdk, or configure with -DARBITERAI_WEB_DASHBOARD=OFF")
endif()

if(NOT Z_VCPKG_ROOT_DIR)
    message(FATAL_ERROR "ARBITERAI_WEB_DASHBOARD needs the build configured with vcpkg's toolchain")
endif()
set(ARBITERAI_VCPKG_TOOLCHAIN "${Z_VCPKG_ROOT_DIR}/scripts/buildsystems/vcpkg.cmake")

include(ExternalProject)

set(ARBITERAI_DASHBOARD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/dashboard")
set(ARBITERAI_DASHBOARD_ASSET_DIR "${CMAKE_BINARY_DIR}/dashboard/assets")
set(ARBITERAI_DASHBOARD_ASSETS
    "${ARBITERAI_DASHBOARD_ASSET_DIR}/shell.html"
    "${ARBITERAI_DASHBOARD_ASSET_DIR}/app.js"
    "${ARBITERAI_DASHBOARD_ASSET_DIR}/app.wasm"
    "${ARBITERAI_DASHBOARD_ASSET_DIR}/app.data")

# BUILD_ALWAYS: the sub-builds track their own sources; their no-op build is
# quick, and the install only rewrites what changed
ExternalProject_Add(dashboard_web
    SOURCE_DIR "${ARBITERAI_DASHBOARD_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/dashboard/wasm"
    INSTALL_DIR "${ARBITERAI_DASHBOARD_ASSET_DIR}"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_TOOLCHAIN_FILE=${ARBITERAI_VCPKG_TOOLCHAIN}
        -DVCPKG_TARGET_TRIPLET=wasm32-emscripten
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${ARBITERAI_EMSCRIPTEN_TOOLCHAIN}
    BUILD_ALWAYS ON
    BUILD_BYPRODUCTS ${ARBITERAI_DASHBOARD_ASSETS}
)

set(ARBITERAI_DASHBOARD_TESTS "${CMAKE_BINARY_DIR}/dashboard/native/dashboard_tests")

ExternalProject_Add(dashboard_native
    SOURCE_DIR "${ARBITERAI_DASHBOARD_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/dashboard/native"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_TOOLCHAIN_FILE=${ARBITERAI_VCPKG_TOOLCHAIN}
        -DVCPKG_TARGET_TRIPLET=x64-linux
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}
    BUILD_ALWAYS ON
    BUILD_BYPRODUCTS "${ARBITERAI_DASHBOARD_TESTS}"
    INSTALL_COMMAND ""
)

message(STATUS "Web dashboard: ON (assets in ${ARBITERAI_DASHBOARD_ASSET_DIR}, tests ${ARBITERAI_DASHBOARD_TESTS})")
