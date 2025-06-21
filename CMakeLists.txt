cmake_minimum_required(VERSION 3.15)
project(hermesaxiom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")

# Find dependencies
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(hermes_src
    ./src/hermesaxiom/hermesaxiom.h
    ./src/hermesaxiom/hermesaxiom.cpp
    ./src/hermesaxiom/modelManager.h
    ./src/hermesaxiom/modelManager.cpp
    ./src/hermesaxiom/providers/base_llm.h
    ./src/hermesaxiom/providers/base_llm.cpp
    ./src/hermesaxiom/providers/openai_llm.h
    ./src/hermesaxiom/providers/openai_llm.cpp
    ./src/hermesaxiom/providers/anthropic_llm.h
    ./src/hermesaxiom/providers/anthropic_llm.cpp
    ./src/hermesaxiom/providers/deepseek_llm.h
    ./src/hermesaxiom/providers/deepseek_llm.cpp
)

# Add library
add_library(hermesaxiom
    ${hermes_src}
)

target_include_directories(hermesaxiom 
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hermesaxiom PUBLIC
    cpr::cpr
    nlohmann_json::nlohmann_json
)
