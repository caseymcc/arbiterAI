# sherpa-onnx (next-gen Kaldi) offline/streaming STT via onnxruntime. Unlike the
# ggml engines it has no ggml, so no conflict with the llama port. Its CMake
# downloads a PREBUILT onnxruntime at configure time (no from-source build).
#
# Cloned via git (pinned tag) so no source SHA512 is needed.

file(MAKE_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src")

set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/sherpa-onnx-v${VERSION}")
if(NOT EXISTS "${SOURCE_PATH}/.git")
    file(REMOVE_RECURSE "${SOURCE_PATH}")
    vcpkg_execute_required_process(
        COMMAND git clone --depth 1 --branch v${VERSION} https://github.com/k2-fsa/sherpa-onnx.git "${SOURCE_PATH}"
        WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}/src"
        LOGNAME clone-sherpa-onnx
    )
endif()

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" _shared)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
      -DBUILD_SHARED_LIBS=${_shared}
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      # sherpa-onnx fetches its deps (kaldi-native-fbank, kaldi-decoder,
      # sentencepiece, onnxruntime, ...) via FetchContent at configure time.
      # vcpkg forces FETCHCONTENT_FULLY_DISCONNECTED=ON (no network); re-enable
      # it here so those downloads happen. (This OPTION is placed after vcpkg's,
      # so it wins.)
      -DFETCHCONTENT_FULLY_DISCONNECTED=OFF
      -DSHERPA_ONNX_ENABLE_C_API=ON
      -DSHERPA_ONNX_ENABLE_TTS=OFF        # STT only — skips piper/espeak-ng/jieba deps
      -DSHERPA_ONNX_ENABLE_PYTHON=OFF
      -DSHERPA_ONNX_ENABLE_TESTS=OFF
      -DSHERPA_ONNX_ENABLE_CHECK=OFF
      -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF
      -DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF
      -DSHERPA_ONNX_ENABLE_ALSA=OFF
      -DSHERPA_ONNX_ENABLE_GPU=OFF
      -DSHERPA_ONNX_ENABLE_BINARY=OFF
      -DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF
)

vcpkg_cmake_install()

if(EXISTS "${CURRENT_PACKAGES_DIR}/lib/cmake/sherpa-onnx")
    vcpkg_cmake_config_fixup(PACKAGE_NAME sherpa-onnx CONFIG_PATH "lib/cmake/sherpa-onnx")
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

if(EXISTS "${SOURCE_PATH}/LICENSE")
    vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
endif()
