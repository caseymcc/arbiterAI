vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ggml-org/llama.cpp
    REF b${VERSION}
    SHA512 b05f130a2052d3c2cec483c3b098f71585fe7d00fa1971786c0a646717f82320211801780625b9aabc9fc1e1797f8995381e40661f3e8a115c72710f147083cd
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
      -DGGML_CCACHE=OFF
      -DGGML_VULKAN=ON
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