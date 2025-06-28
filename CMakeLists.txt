cmake_minimum_required(VERSION 3.15)
project(hermesaxiom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")

# Find dependencies
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(llama CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_path(PICOSHA2_INCLUDE_DIRS "picosha2.h")

set(hermes_src
    ./src/hermesaxiom/hermesaxiom.h
    ./src/hermesaxiom/hermesaxiom.cpp
    ./src/hermesaxiom/modelManager.h
    ./src/hermesaxiom/modelManager.cpp
    ./src/hermesaxiom/modelDownloader.h
    ./src/hermesaxiom/modelDownloader.cpp
    ./src/hermesaxiom/providers/baseProvider.h
    ./src/hermesaxiom/providers/baseProvider.cpp
    ./src/hermesaxiom/providers/openai.h
    ./src/hermesaxiom/providers/openai.cpp
    ./src/hermesaxiom/providers/anthropic.h
    ./src/hermesaxiom/providers/anthropic.cpp
    ./src/hermesaxiom/providers/deepseek.h
    ./src/hermesaxiom/providers/deepseek.cpp
    ./src/hermesaxiom/providers/llama.h
    ./src/hermesaxiom/providers/llama.cpp
    ./src/hermesaxiom/providers/llamaInterface.h
    ./src/hermesaxiom/providers/llamaInterface.cpp
)

# Add library
add_library(hermesaxiom
    ${hermes_src}
)

target_include_directories(hermesaxiom
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
       ${PICOSHA2_INCLUDE_DIRS}
)

target_link_libraries(hermesaxiom 
#   PUBLIC
    PRIVATE
        cpr::cpr
        nlohmann_json::nlohmann_json
        llama
        spdlog::spdlog
)
