cmake_minimum_required(VERSION 3.21)

# The arbiterAI-server dashboard: a gweni app built twice from this directory,
# for wasm32-emscripten (app.js/app.wasm/app.data, embedded in the server) and
# for x64-linux (dashboard_tests, the panels rendered headless from fixtures).
project(arbiterAI-dashboard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(gweni CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# the app, its panels and its data layer: no Emscripten, built for both
add_library(dashboard_core STATIC
    app/dashboardApp.h
    app/dashboardApp.cpp
    data/dataSource.h
    data/dataSource.cpp
    data/fixtureDataSource.h
    data/fixtureDataSource.cpp
    panels/widgets.h
    panels/widgets.cpp
    panels/overviewPanel.h
    panels/overviewPanel.cpp
    panels/logPanel.h
    panels/logPanel.cpp
)
target_include_directories(dashboard_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dashboard_core PUBLIC gweni::gweni nlohmann_json::nlohmann_json)

if(EMSCRIPTEN)
    add_executable(dashboard_web
        web/main.cpp
        web/fetchDataSource.h
        web/fetchDataSource.cpp
    )
    target_link_libraries(dashboard_web PRIVATE dashboard_core gweni::app)
    set_target_properties(dashboard_web PROPERTIES OUTPUT_NAME app SUFFIX ".js")
    target_link_options(dashboard_web PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=4MB
        -sFETCH=1
        -sEXPORTED_FUNCTIONS=_main,_malloc,_free)
    gweni_add_resources(dashboard_web)
    install(FILES
        $<TARGET_FILE_DIR:dashboard_web>/app.js
        $<TARGET_FILE_DIR:dashboard_web>/app.wasm
        $<TARGET_FILE_DIR:dashboard_web>/app.data
        web/shell.html
        DESTINATION .)
else()
    find_package(GTest CONFIG REQUIRED)
    add_executable(dashboard_tests
        tests/main.cpp
        tests/smokeTests.cpp
    )
    target_link_libraries(dashboard_tests PRIVATE dashboard_core gweni::app GTest::gtest)
    gweni_add_resources(dashboard_tests)
endif()
