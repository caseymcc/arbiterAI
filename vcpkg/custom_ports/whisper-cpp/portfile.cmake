# whisper.cpp shares the ggml build system with llama.cpp. If whisper built and
# installed its own ggml it would collide with the llama-cpp port (both own
# include/ggml.h, lib/libggml*, share/ggml). To coexist we depend on llama-cpp
# and build whisper against that already-installed ggml via WHISPER_USE_SYSTEM_GGML,
# so exactly one ggml is present. This also keeps a single ggml version in the
# tree. NOTE: whisper's expected ggml API must be compatible with the pinned
# llama-cpp build; bump the two together if the whisper build fails to compile
# against the installed ggml.

file(MAKE_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src")

if("head" IN_LIST FEATURES)
    set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/whisper-cpp-master")
    if(NOT EXISTS "${SOURCE_PATH}/.git")
        file(REMOVE_RECURSE "${SOURCE_PATH}")
        vcpkg_execute_required_process(
            COMMAND git clone --depth 1 https://github.com/ggml-org/whisper.cpp.git "${SOURCE_PATH}"
            WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src"
            LOGNAME clone-whisper-cpp
        )
    else()
        vcpkg_execute_required_process(
            COMMAND git pull --ff-only
            WORKING_DIRECTORY "${SOURCE_PATH}"
            LOGNAME pull-whisper-cpp
        )
    endif()
else()
    # Pinned release tag, cloned via git so no source SHA512 is required.
    set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/whisper-cpp-v${VERSION}")
    if(NOT EXISTS "${SOURCE_PATH}/.git")
        file(REMOVE_RECURSE "${SOURCE_PATH}")
        vcpkg_execute_required_process(
            COMMAND git clone --depth 1 --branch v${VERSION} https://github.com/ggml-org/whisper.cpp.git "${SOURCE_PATH}"
            WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src"
            LOGNAME clone-whisper-cpp
        )
    endif()
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
      -DGGML_CCACHE=OFF
      -DGGML_VULKAN=ON
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DWHISPER_BUILD_TESTS=OFF
      -DWHISPER_BUILD_EXAMPLES=OFF
      -DWHISPER_BUILD_SERVER=OFF
      ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME whisper CONFIG_PATH "lib/cmake/whisper")

# whisper bundles and stages its own ggml, which collides with the ggml already
# installed by the llama-cpp dependency (WHISPER_USE_SYSTEM_GGML is a no-op in
# this whisper release). Strip whisper's ggml so exactly the llama ggml remains;
# whisper's static lib resolves ggml symbols against it at final link. The two
# ports' ggml versions must stay compatible — bump them together if this breaks.
file(GLOB _whisper_ggml_headers "${CURRENT_PACKAGES_DIR}/include/ggml*.h")
file(REMOVE ${_whisper_ggml_headers})
file(GLOB _whisper_ggml_libs
    "${CURRENT_PACKAGES_DIR}/lib/libggml*"
    "${CURRENT_PACKAGES_DIR}/debug/lib/libggml*")
file(REMOVE ${_whisper_ggml_libs})
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/share/ggml"
    "${CURRENT_PACKAGES_DIR}/debug/share/ggml")
file(GLOB _whisper_ggml_pc
    "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/*ggml*"
    "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/*ggml*")
file(REMOVE ${_whisper_ggml_pc})
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

if (VCPKG_LIBRARY_LINKAGE MATCHES "static")
    # Static builds ship no bin/ dir; drop the set_and_check that would fail.
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/whisper/whisper-config.cmake"
        "set_and_check\\(WHISPER_BIN_DIR[^\"]*\"\\$\\{PACKAGE_PREFIX_DIR\\}/bin\"\\)"
        ""
        REGEX
    )
endif()

vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/whisper/whisper-config.cmake"
    "add_library(whisper UNKNOWN IMPORTED)"
    "if (NOT TARGET whisper)
    add_library(whisper UNKNOWN IMPORTED)
endif()
"
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

if (VCPKG_LIBRARY_LINKAGE MATCHES "static")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
