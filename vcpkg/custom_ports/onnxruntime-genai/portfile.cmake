# onnxruntime-genai ships prebuilt release archives that already bundle the
# ONNX Runtime they need, so there is nothing to compile here.  Building it
# from source would mean building ONNX Runtime too, for no benefit: the
# execution providers we care about (including the Ryzen AI NPU one) are
# loaded at runtime rather than baked in.
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER disabled)

set(OGA_VERSION "0.15.2")
set(OGA_DIR "onnxruntime-genai-${OGA_VERSION}-linux-x64")

vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/microsoft/onnxruntime-genai/releases/download/v${OGA_VERSION}/${OGA_DIR}.tar.gz"
    FILENAME "${OGA_DIR}.tar.gz"
    SHA512 021bea7f3fd17dfb53ac8e060f5ba73b020531165b1d90b77d9c0f750f4cf1c96fcb4d670b2b6d691263713b09b9effe7421d6e144a3ba01e7bd070b3c65e41e
)

vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)

file(INSTALL "${SOURCE_PATH}/${OGA_DIR}/include/ort_genai.h"
             "${SOURCE_PATH}/${OGA_DIR}/include/ort_genai_c.h"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# A shared library: it dlopen's its execution providers, so it cannot be
# archived into arbiterAI.  It has to travel with the binary when deploying.
file(INSTALL "${SOURCE_PATH}/${OGA_DIR}/lib/libonnxruntime-genai.so"
     DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SOURCE_PATH}/${OGA_DIR}/lib/libonnxruntime-genai.so"
     DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/${OGA_DIR}/LICENSE")
