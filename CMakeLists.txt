cmake_minimum_required(VERSION 3.15)
project(arbiterAI VERSION 0.1.0 LANGUAGES C CXX)

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
find_package(llama CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_path(PICOSHA2_INCLUDE_DIRS "picosha2.h")
find_package(OpenSSL REQUIRED)
find_package(httplib CONFIG REQUIRED)

set(arbiterai_src
    ./src/arbiterAI/arbiterAI.h
    ./src/arbiterAI/arbiterAI.cpp
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
    ./src/arbiterAI/providers/baseProvider.h
    ./src/arbiterAI/providers/baseProvider.cpp
    ./src/arbiterAI/providers/openai.h
    ./src/arbiterAI/providers/openai.cpp
    ./src/arbiterAI/providers/anthropic.h
    ./src/arbiterAI/providers/anthropic.cpp
    ./src/arbiterAI/providers/deepseek.h
    ./src/arbiterAI/providers/deepseek.cpp
    ./src/arbiterAI/providers/llama.h
    ./src/arbiterAI/providers/llama.cpp
    ./src/arbiterAI/providers/llamaInterface.h
    ./src/arbiterAI/providers/llamaInterface.cpp
   ./src/arbiterAI/providers/openrouter.h
   ./src/arbiterAI/providers/openrouter.cpp
)

# Add library
add_library(arbiterai
    ${arbiterai_src}
)

target_include_directories(arbiterai
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
        ${LLAMA_INCLUDE_DIRS}
    PRIVATE
        ${PICOSHA2_INCLUDE_DIRS}
)

target_link_libraries(arbiterai
    PUBLIC
        cpr::cpr
        nlohmann_json::nlohmann_json
        llama
        libgit2::libgit2package
    PRIVATE
        spdlog::spdlog
        Threads::Threads
        nlohmann_json_schema_validator::validator
        OpenSSL::SSL
        OpenSSL::Crypto
)
 
 if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(arbiterai_tests
        tests/main.cpp
        tests/modelManagerTests.cpp
        tests/modelDownloaderTests.cpp
        tests/configDownloaderTests.cpp
        tests/arbiterAITests.cpp
    )
    
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
