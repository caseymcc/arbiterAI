cmake_minimum_required(VERSION 3.15)
project(arbiterAI VERSION 0.1.13 LANGUAGES C CXX)

option(ARBITERAI_ENABLE_LLAMA "Enable local llama.cpp model runtime support" ON)
option(ARBITERAI_ENABLE_WHISPER "Enable local whisper.cpp speech-to-text support" OFF)
option(ARBITERAI_ENABLE_STABLE_DIFFUSION "Enable local stable-diffusion.cpp image generation support" OFF)
option(ARBITERAI_ENABLE_VIBEVOICE "Enable local vibevoice.cpp text-to-speech support" OFF)
option(ARBITERAI_ENABLE_ORPHEUS "Enable local Orpheus TTS via a dlopen'd chatllm.cpp engine library" OFF)

# Read llama-cpp build number from our custom vcpkg port
if(ARBITERAI_ENABLE_LLAMA)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/custom_ports/llama-cpp/vcpkg.json" LLAMA_PORT_JSON)
    string(JSON ARBITERAI_LLAMACPP_BUILD GET "${LLAMA_PORT_JSON}" "version")
else()
    set(ARBITERAI_LLAMACPP_BUILD "disabled")
endif()

# Generate version header from template
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/arbiterAI/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/arbiterAI/version.h"
    @ONLY
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(VERSION_CONFIG_PATH "${CMAKE_CURRENT_BINARY_DIR}/cmake")
set(PROJECT_VERSION_CONFIG_FILE "${VERSION_CONFIG_PATH}/arbiterAIConfigVersion.cmake")
set(PROJECT_CONFIG_FILE "${VERSION_CONFIG_PATH}/arbiterAIConfig.cmake")

write_basic_package_version_file(
    "${PROJECT_VERSION_CONFIG_FILE}"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arbiterAIConfig.cmake.in"
    "${PROJECT_CONFIG_FILE}"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/arbiterAI"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")
option(BUILD_TESTING "Build the tests" ON)

# Find dependencies
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
if(ARBITERAI_ENABLE_LLAMA)
    find_package(llama CONFIG REQUIRED)
endif()
if(ARBITERAI_ENABLE_WHISPER)
    find_package(whisper CONFIG REQUIRED)
endif()
if(ARBITERAI_ENABLE_STABLE_DIFFUSION)
    find_package(stable-diffusion CONFIG REQUIRED)
    find_package(ZLIB REQUIRED)
endif()
if(ARBITERAI_ENABLE_VIBEVOICE)
    # vibevoice.cpp installs a lib + header but no package config.
    find_path(VIBEVOICE_INCLUDE_DIR NAMES vibevoice_capi.h REQUIRED)
    find_library(VIBEVOICE_LIBRARY NAMES vibevoice REQUIRED)
endif()
find_package(libgit2 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_path(PICOSHA2_INCLUDE_DIRS "picosha2.h")
find_package(OpenSSL REQUIRED)
find_package(httplib CONFIG REQUIRED)

set(arbiterai_src
    ./src/arbiterAI/arbiterAI.h
    ./src/arbiterAI/arbiterAI.cpp
    ./src/arbiterAI/chatClient.h
    ./src/arbiterAI/chatClient.cpp
    ./src/arbiterAI/cacheManager.h
    ./src/arbiterAI/cacheManager.cpp
    ./src/arbiterAI/costManager.h
    ./src/arbiterAI/costManager.cpp
    ./src/arbiterAI/modelManager.h
    ./src/arbiterAI/modelManager.cpp
    ./src/arbiterAI/modelDownloader.h
    ./src/arbiterAI/modelDownloader.cpp
    ./src/arbiterAI/fileVerifier.h
    ./src/arbiterAI/fileVerifier.cpp
    ./src/arbiterAI/configDownloader.h
    ./src/arbiterAI/configDownloader.cpp
    ./src/arbiterAI/hardwareDetector.h
    ./src/arbiterAI/hardwareDetector.cpp
    ./src/arbiterAI/modelFitCalculator.h
    ./src/arbiterAI/modelFitCalculator.cpp
    ./src/arbiterAI/telemetryCollector.h
    ./src/arbiterAI/telemetryCollector.cpp
    ./src/arbiterAI/storageManager.h
    ./src/arbiterAI/storageManager.cpp
    ./src/arbiterAI/providers/baseProvider.h
    ./src/arbiterAI/providers/baseProvider.cpp
    ./src/arbiterAI/providers/openai.h
    ./src/arbiterAI/providers/openai.cpp
    ./src/arbiterAI/providers/anthropic.h
    ./src/arbiterAI/providers/anthropic.cpp
    ./src/arbiterAI/providers/deepseek.h
    ./src/arbiterAI/providers/deepseek.cpp
    ./src/arbiterAI/providers/mock.h
    ./src/arbiterAI/providers/mock.cpp
   ./src/arbiterAI/providers/openrouter.h
   ./src/arbiterAI/providers/openrouter.cpp
)

if(ARBITERAI_ENABLE_LLAMA)
    list(APPEND arbiterai_src
        ./src/arbiterAI/modelRuntime.h
        ./src/arbiterAI/modelRuntime.cpp
        ./src/arbiterAI/inferenceScheduler.h
        ./src/arbiterAI/inferenceScheduler.cpp
        ./src/arbiterAI/providers/llama.h
        ./src/arbiterAI/providers/llama.cpp
    )
endif()

if(ARBITERAI_ENABLE_WHISPER)
    list(APPEND arbiterai_src
        ./src/arbiterAI/providers/whisper.h
        ./src/arbiterAI/providers/whisper.cpp
    )
endif()

if(ARBITERAI_ENABLE_STABLE_DIFFUSION)
    list(APPEND arbiterai_src
        ./src/arbiterAI/providers/stableDiffusion.h
        ./src/arbiterAI/providers/stableDiffusion.cpp
    )
endif()

if(ARBITERAI_ENABLE_VIBEVOICE)
    list(APPEND arbiterai_src
        ./src/arbiterAI/providers/vibevoice.h
        ./src/arbiterAI/providers/vibevoice.cpp
    )
endif()

if(ARBITERAI_ENABLE_ORPHEUS)
    # dlopen-based: needs only libdl (already linked via CMAKE_DL_LIBS).
    list(APPEND arbiterai_src
        ./src/arbiterAI/providers/orpheus.h
        ./src/arbiterAI/providers/orpheus.cpp
    )
endif()

# Add library
add_library(arbiterai
    ${arbiterai_src}
)

# Set PIC for this library
set_property(TARGET arbiterai PROPERTY POSITION_INDEPENDENT_CODE ON)

target_include_directories(arbiterai
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PICOSHA2_INCLUDE_DIRS}
)

if(ARBITERAI_ENABLE_LLAMA)
    target_include_directories(arbiterai PUBLIC ${LLAMA_INCLUDE_DIRS})
    target_compile_definitions(arbiterai PUBLIC ARBITERAI_ENABLE_LLAMA=1)
endif()

if(ARBITERAI_ENABLE_WHISPER)
    target_compile_definitions(arbiterai PUBLIC ARBITERAI_ENABLE_WHISPER=1)
endif()

if(ARBITERAI_ENABLE_STABLE_DIFFUSION)
    target_compile_definitions(arbiterai PUBLIC ARBITERAI_ENABLE_STABLE_DIFFUSION=1)
endif()

if(ARBITERAI_ENABLE_VIBEVOICE)
    target_include_directories(arbiterai PRIVATE ${VIBEVOICE_INCLUDE_DIR})
    target_compile_definitions(arbiterai PUBLIC ARBITERAI_ENABLE_VIBEVOICE=1)
endif()

if(ARBITERAI_ENABLE_ORPHEUS)
    target_compile_definitions(arbiterai PUBLIC ARBITERAI_ENABLE_ORPHEUS=1)
endif()

target_link_libraries(arbiterai
    PUBLIC
        cpr::cpr
        nlohmann_json::nlohmann_json
        $<$<BOOL:${ARBITERAI_ENABLE_LLAMA}>:llama>
        $<$<BOOL:${ARBITERAI_ENABLE_WHISPER}>:whisper>
        $<$<BOOL:${ARBITERAI_ENABLE_STABLE_DIFFUSION}>:stable-diffusion>
        $<$<BOOL:${ARBITERAI_ENABLE_STABLE_DIFFUSION}>:ZLIB::ZLIB>
        $<$<BOOL:${ARBITERAI_ENABLE_VIBEVOICE}>:${VIBEVOICE_LIBRARY}>
        libgit2::libgit2package
    PRIVATE
        spdlog::spdlog
        Threads::Threads
        nlohmann_json_schema_validator::validator
        OpenSSL::SSL
        OpenSSL::Crypto
        ${CMAKE_DL_LIBS}
)
 
 if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(arbiterai_tests
        tests/main.cpp
        tests/modelManagerTests.cpp
        tests/modelDownloaderTests.cpp
        tests/configDownloaderTests.cpp
        tests/arbiterAITests.cpp
        tests/chatClientTests.cpp
        tests/providerTests.cpp
        tests/mockProviderTests.cpp
        tests/hardwareDetectorTests.cpp
        tests/telemetryCollectorTests.cpp
        tests/storageManagerTests.cpp
        tests/serverConnectTests.cpp
    )

    if(ARBITERAI_ENABLE_LLAMA)
        target_sources(arbiterai_tests PRIVATE
            tests/modelRuntimeTests.cpp
            tests/llamaProviderTests.cpp
            tests/inferenceSchedulerTests.cpp
        )
    endif()

    if(ARBITERAI_ENABLE_WHISPER)
        target_sources(arbiterai_tests PRIVATE
            tests/whisperProviderTests.cpp
        )
    endif()

    if(ARBITERAI_ENABLE_STABLE_DIFFUSION)
        target_sources(arbiterai_tests PRIVATE
            tests/stableDiffusionProviderTests.cpp
        )
    endif()

    if(ARBITERAI_ENABLE_VIBEVOICE)
        target_sources(arbiterai_tests PRIVATE
            tests/vibevoiceProviderTests.cpp
        )
    endif()

    if(ARBITERAI_ENABLE_ORPHEUS)
        target_sources(arbiterai_tests PRIVATE
            tests/orpheusProviderTests.cpp
        )
    endif()
    
    target_link_libraries(arbiterai_tests
        PRIVATE
            arbiterai
            GTest::gtest
            GTest::gmock
    )
endif()

add_executable(arbiterAI-cli
    examples/cli/main.cpp
)

target_link_libraries(arbiterAI-cli
    PRIVATE
        arbiterai
        cxxopts::cxxopts
)

add_executable(arbiterAI-proxy
    examples/proxy/main.cpp
)

target_link_libraries(arbiterAI-proxy
    PRIVATE
        arbiterai
        httplib::httplib
)

add_executable(arbiterAI-server
    src/server/main.cpp
    src/server/routes.h
    src/server/routes.cpp
    src/server/dashboard.h
)

target_include_directories(arbiterAI-server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)

target_link_libraries(arbiterAI-server
    PRIVATE
        arbiterai
        httplib::httplib
        spdlog::spdlog
)

install(
    TARGETS arbiterai
    EXPORT arbiterAITargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY src/arbiterAI/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/arbiterAI/version.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/arbiterAI"
)

install(
    FILES
        "${PROJECT_CONFIG_FILE}"
        "${PROJECT_VERSION_CONFIG_FILE}"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/arbiterAI"
)

install(
    EXPORT arbiterAITargets
    FILE arbiterAITargets.cmake
    NAMESPACE arbiterAI::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/arbiterAI"
)
