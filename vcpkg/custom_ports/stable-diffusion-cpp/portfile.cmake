# stable-diffusion.cpp is a ggml sibling of llama.cpp/whisper.cpp. Like the
# whisper port, it bundles and stages its own ggml which would collide with the
# ggml installed by the llama-cpp dependency. We strip sd's staged ggml and link
# against the llama ggml so exactly one copy exists. The two ggml versions must
# stay compatible — bump both ports together if the final link breaks.
#
# sd.cpp has no pinned release tags, so we clone master (depth 1). Pin to a
# specific commit here if reproducibility matters.

file(MAKE_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src")

set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/stable-diffusion-cpp-master")
if(NOT EXISTS "${SOURCE_PATH}/.git")
    file(REMOVE_RECURSE "${SOURCE_PATH}")
    vcpkg_execute_required_process(
        COMMAND git clone --depth 1 --recursive https://github.com/leejet/stable-diffusion.cpp.git "${SOURCE_PATH}"
        WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src"
        LOGNAME clone-stable-diffusion-cpp
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
      -DGGML_CCACHE=OFF
      -DSD_VULKAN=ON
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DSD_BUILD_SHARED_LIBS=OFF
      -DSD_BUILD_EXAMPLES=OFF
      ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()

if(EXISTS "${CURRENT_PACKAGES_DIR}/lib/cmake/stable-diffusion")
    vcpkg_cmake_config_fixup(PACKAGE_NAME stable-diffusion CONFIG_PATH "lib/cmake/stable-diffusion")
endif()

# Strip sd's bundled ggml (provided by llama-cpp instead).
file(GLOB _sd_ggml_headers
    "${CURRENT_PACKAGES_DIR}/include/ggml*.h"
    "${CURRENT_PACKAGES_DIR}/include/gguf*.h")
file(REMOVE ${_sd_ggml_headers})
file(GLOB _sd_ggml_libs
    "${CURRENT_PACKAGES_DIR}/lib/libggml*"
    "${CURRENT_PACKAGES_DIR}/debug/lib/libggml*")
file(REMOVE ${_sd_ggml_libs})
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

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
