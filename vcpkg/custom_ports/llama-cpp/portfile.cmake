if("head" IN_LIST FEATURES)
    # Build from latest master HEAD — use git clone directly
    # vcpkg_from_github with HEAD_REF only works with `--head` flag,
    # so we clone manually for manifest-mode compatibility.
    set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/llama-cpp-master")
    if(NOT EXISTS "${SOURCE_PATH}/.git")
        file(REMOVE_RECURSE "${SOURCE_PATH}")
        vcpkg_execute_required_process(
            COMMAND git clone --depth 1 https://github.com/ggml-org/llama.cpp.git "${SOURCE_PATH}"
            WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src"
            LOGNAME clone-llama-cpp
        )
    else()
        vcpkg_execute_required_process(
            COMMAND git pull --ff-only
            WORKING_DIRECTORY "${SOURCE_PATH}"
            LOGNAME pull-llama-cpp
        )
    endif()
else()
    # Build from a pinned release tag
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO ggml-org/llama.cpp
        REF b${VERSION}
        SHA512 6be3482ef58872ee4a386ba831175e53ce0d93c6992e4389ffd97f9af3cc7becdd1356fda575702681f55261e7fe81bc1baa12edd0d5f809aa80684f5c890bac
        HEAD_REF master
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
      -DGGML_CCACHE=OFF
      -DGGML_VULKAN=ON
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DLLAMA_BUILD_TESTS=OFF
      -DLLAMA_BUILD_EXAMPLES=OFF
      -DLLAMA_BUILD_TOOLS=OFF
      -DLLAMA_BUILD_SERVER=OFF
      -DLLAMA_ALL_WARNINGS=OFF
      ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME llama CONFIG_PATH "lib/cmake/llama" DO_NOT_DELETE_PARENT_CONFIG_PATH)
vcpkg_cmake_config_fixup(PACKAGE_NAME ggml  CONFIG_PATH "lib/cmake/ggml")
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

if (VCPKG_LIBRARY_LINKAGE MATCHES "static")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/llama/llama-config.cmake"
        "set_and_check\\(LLAMA_BIN_DIR[^\"]*\"\\$\\{PACKAGE_PREFIX_DIR\\}/bin\"\\)"
        ""
        REGEX
    )
endif()

vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/llama/llama-config.cmake"
    "add_library(llama UNKNOWN IMPORTED)"
    "if (NOT TARGET llama)
    add_library(llama UNKNOWN IMPORTED)
endif()
"
)

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/tools/${PORT}")
file(RENAME "${CURRENT_PACKAGES_DIR}/bin/convert_hf_to_gguf.py" "${CURRENT_PACKAGES_DIR}/tools/${PORT}/convert-hf-to-gguf.py")
file(INSTALL "${SOURCE_PATH}/gguf-py" DESTINATION "${CURRENT_PACKAGES_DIR}/tools/${PORT}")
if (NOT VCPKG_BUILD_TYPE)
    file(REMOVE "${CURRENT_PACKAGES_DIR}/debug/bin/convert_hf_to_gguf.py")
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

if (VCPKG_LIBRARY_LINKAGE MATCHES "static")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")