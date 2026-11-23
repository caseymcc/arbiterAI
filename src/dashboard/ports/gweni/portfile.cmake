#
#  Gweni
#  Copyright (c) 2010 Facepunch Studios
#  MIT License, see LICENSE
#
#  gweni's vcpkg port (ports/gweni/ of caseymcc/gweni), carried by arbiterAI
#  as an overlay until gweni publishes its vcpkg-registry branch: pinned to a
#  gweni main commit, with an "app" feature that builds gweni::app (the
#  window/WebGL shell and app::Headless) which the upstream port leaves out.
#

# export.h defines GWENI_EXPORT empty: there is no shared gweni
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO caseymcc/gweni
    REF 9b52b0d64005d761b28adcabf7e52654140796b1
    SHA512 2c9f3d275986c189643b99d3e718fb1ec466653915acb1aab61f403b66da5ffb6f5fbf2b68bd7c34243e2d379bebe033f7304495cf1d9f8ae2d9104180d04714
    HEAD_REF main
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        opengl GWENI_RENDER_OPENGL_CORE
        vulkan GWENI_RENDER_VULKAN
        app GWENI_BUILD_APP
)

set(GWENI_PORT_OPTIONS "")
if("vulkan" IN_LIST FEATURES)
    # glslang is a host dependency: its glslangValidator runs at build time
    list(APPEND GWENI_PORT_OPTIONS
        "-DGWENI_GLSLANG_VALIDATOR=${CURRENT_HOST_INSTALLED_DIR}/tools/glslang/glslangValidator${VCPKG_HOST_EXECUTABLE_SUFFIX}")
endif()

# The library alone: without the examples and the tests nothing is downloaded
# (cmake/LazyFont.cmake) and no test-only dependency is looked for. The app
# shell (gweni_app, gweni::app: GLFW and natively EGL) only with the "app"
# feature
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        ${GWENI_PORT_OPTIONS}
        -DGWENI_BUILD_EXAMPLES=OFF
        -DGWENI_BUILD_TESTS=OFF
)

vcpkg_cmake_install()
# gweniConfig.cmake is in share/gweni already, with the skin's resources/
# beside it (GWENI_RESOURCE_DIR)
vcpkg_cmake_config_fixup()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

# libgweni.a holds the vendored freetype-gl's (BSD-2-Clause) and imglib's (MIT)
# objects, and share/gweni/resources the fonts (Apache-2.0, Bitstream Vera)
vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/LICENSE"
    "${SOURCE_PATH}/deps/freetype-gl/LICENSE"
    "${SOURCE_PATH}/deps/imglib/LICENSE"
    "${SOURCE_PATH}/resources/OpenSans-LICENSE.txt"
    "${SOURCE_PATH}/resources/DejaVuSansMono-LICENSE.txt")
