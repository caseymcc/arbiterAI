cmake_minimum_required(VERSION 3.15)
project(arbiterai VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")
option(BUILD_TESTING "Build the tests" ON)

# Find dependencies
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
find_package(llama CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_path(PICOSHA2_INCLUDE_DIRS "picosha2.h")

set(arbiterai_src
    ./src/arbiterAI/arbiterAI.h
    ./src/arbiterAI/arbiterAI.cpp
    ./src/arbiterAI/modelManager.h
    ./src/arbiterAI/modelManager.cpp
    ./src/arbiterAI/modelDownloader.h
    ./src/arbiterAI/modelDownloader.cpp
    ./src/arbiterAI/fileVerifier.h
    ./src/arbiterAI/fileVerifier.cpp
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
)

# Add library
add_library(arbiterai
    ${arbiterai_src}
)

target_include_directories(arbiterai
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${LLAMA_INCLUDE_DIRS}
    PRIVATE
        ${PICOSHA2_INCLUDE_DIRS}
)

target_link_libraries(arbiterai
    PUBLIC
        cpr::cpr
        nlohmann_json::nlohmann_json
        llama
    PRIVATE
        spdlog::spdlog
        Threads::Threads
        nlohmann_json_schema_validator::validator
)

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(arbiterai_tests
        tests/main.cpp
        tests/modelManagerTests.cpp
        tests/modelDownloaderTests.cpp
    )
    
    target_link_libraries(arbiterai_tests
        PRIVATE
            arbiterai
            GTest::gtest
            GTest::gmock
    )
endif()

