# Builds liborpheus_engine.so: a single self-contained shared library that
# statically embeds chatllm.cpp + its (forked) ggml with hidden visibility and
# exports ONLY arbiter_orpheus_tts. arbiterAI's Orpheus provider dlopen's it with
# RTLD_LOCAL, so the forked ggml never clashes with the host's llama ggml.
#
# This is a runtime artifact (dlopen'd, not linked), so no headers/CMake config
# are installed — only the .so. See README.md for the design and the ABI.

# chatllm.cpp has no release tags — clone master (--recursive for its ggml
# submodule). Pin a commit here if reproducibility matters.
set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/chatllm-cpp")
if(NOT EXISTS "${SOURCE_PATH}/.git")
    file(MAKE_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src")
    file(REMOVE_RECURSE "${SOURCE_PATH}")
    vcpkg_execute_required_process(
        COMMAND git clone --depth 1 --recursive https://github.com/foldl/chatllm.cpp.git "${SOURCE_PATH}"
        WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src"
        LOGNAME clone-chatllm
    )
endif()

# Drop in the shim + version script.
file(COPY "${CURRENT_PORT_DIR}/shim/orpheus_engine.cpp" DESTINATION "${SOURCE_PATH}/src")
file(COPY "${CURRENT_PORT_DIR}/shim/orpheus_engine.map" DESTINATION "${SOURCE_PATH}")

# Append the engine target to chatllm's CMakeLists (reuses its ${core_files},
# excludes src/main.cpp so we get the Pipeline without the CLI/binding). The shim
# provides log_internal (normally defined in main.cpp).
file(READ "${SOURCE_PATH}/CMakeLists.txt" _cml)
if(NOT _cml MATCHES "orpheus_engine")
    file(APPEND "${SOURCE_PATH}/CMakeLists.txt" "
# --- arbiterAI Orpheus engine ---
add_library(orpheus_engine SHARED src/orpheus_engine.cpp \${core_files})
target_link_libraries(orpheus_engine PRIVATE ggml)
set_target_properties(orpheus_engine PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
    PREFIX \"lib\")
target_link_options(orpheus_engine PRIVATE
    \"-Wl,--version-script=\${CMAKE_CURRENT_SOURCE_DIR}/orpheus_engine.map\")
install(TARGETS orpheus_engine LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
      -DBUILD_SHARED_LIBS=OFF          # static ggml, embedded into the .so
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DGGML_VULKAN=OFF
      -DGGML_NATIVE=OFF
      -DGGML_CCACHE=OFF
)

vcpkg_cmake_build(TARGET orpheus_engine LOGFILE_BASE build-orpheus-engine)

# Install only the .so (dlopen'd at runtime; nothing links it).
file(GLOB _engine_so "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/lib/liborpheus_engine.so*")
if(NOT _engine_so)
    file(GLOB _engine_so "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/lib/liborpheus_engine.so*")
endif()
file(INSTALL ${_engine_so} DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

# Runtime-only artifact: no headers, no import lib, shared object in a static
# triplet — waive the corresponding vcpkg post-build checks.
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
