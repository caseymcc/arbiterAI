# vibevoice.cpp is a ggml sibling (add_subdirectory(third_party/ggml)). Like the
# whisper and stable-diffusion ports, it bundles and stages its own ggml which
# would collide with the ggml installed by the llama-cpp dependency. We strip
# vibevoice's staged ggml and link against the llama ggml so exactly one copy
# exists. Keep the two ggml versions compatible.
#
# The upstream installs a library + header + an EXPORT targets file but no
# package Config, so arbiterAI links it with find_library/find_path rather than
# find_package.
#
# No pinned release tags upstream — clone master (depth 1). Pin a commit here if
# reproducibility matters.

file(MAKE_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src")

set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/vibevoice-cpp-master")
if(NOT EXISTS "${SOURCE_PATH}/.git")
    file(REMOVE_RECURSE "${SOURCE_PATH}")
    vcpkg_execute_required_process(
        COMMAND git clone --depth 1 --recursive https://github.com/localai-org/vibevoice.cpp.git "${SOURCE_PATH}"
        WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src"
        LOGNAME clone-vibevoice-cpp
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
      -DGGML_CCACHE=OFF
      -DVIBEVOICE_GGML_VULKAN=ON
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DVIBEVOICE_SHARED=OFF
      -DVIBEVOICE_BUILD_TESTS=OFF
      -DVIBEVOICE_BUILD_EXAMPLES=OFF
      -DVIBEVOICE_BUILD_SERVER=OFF
      ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()

# Strip vibevoice's bundled ggml (provided by llama-cpp instead).
file(GLOB _vv_ggml_headers
    "${CURRENT_PACKAGES_DIR}/include/ggml*.h"
    "${CURRENT_PACKAGES_DIR}/include/gguf*.h")
file(REMOVE ${_vv_ggml_headers})
file(GLOB _vv_ggml_libs
    "${CURRENT_PACKAGES_DIR}/lib/libggml*"
    "${CURRENT_PACKAGES_DIR}/debug/lib/libggml*")
file(REMOVE ${_vv_ggml_libs})
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/share/ggml"
    "${CURRENT_PACKAGES_DIR}/debug/share/ggml"
    "${CURRENT_PACKAGES_DIR}/lib/cmake/ggml"
    "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/ggml"
    "${CURRENT_PACKAGES_DIR}/lib/cmake/gguf"
    "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/gguf")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

if (VCPKG_LIBRARY_LINKAGE MATCHES "static")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

if(EXISTS "${SOURCE_PATH}/LICENSE")
    vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
endif()
