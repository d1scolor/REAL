cmake_minimum_required(VERSION 3.12)

project(miniant-real)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)
option(REAL_ENABLE_UPDATER "Build the legacy curl-based auto-updater" OFF)

FILE(GLOB_RECURSE REAL_APP_INPUT src/*.cpp src/*.h res/*.rc res/*.h)

if (NOT REAL_ENABLE_UPDATER)
    list(FILTER REAL_APP_INPUT EXCLUDE REGEX ".*[/\\\\]CurlWrapper[/\\\\].*")
endif()

add_executable(real-app WIN32 ${REAL_APP_INPUT})

target_compile_definitions(real-app PUBLIC _UNICODE UNICODE)
target_compile_definitions(real-app PRIVATE REAL_ENABLE_UPDATER=$<BOOL:${REAL_ENABLE_UPDATER}>)
target_compile_options(real-app PRIVATE /JMC)

set(CMAKE_PREFIX_PATH "deps")

if (REAL_ENABLE_UPDATER)
    find_package(curl REQUIRED CONFIG)
    target_link_libraries(real-app curl)
endif()

find_package(expected REQUIRED)
target_link_libraries(real-app expected)

find_package(json REQUIRED)
target_link_libraries(real-app json)

find_package(spdlog REQUIRED)
target_link_libraries(real-app spdlog)

target_link_libraries(real-app ole32 uuid propsys avrt shell32)

source_group(TREE . FILES ${REAL_APP_INPUT})
