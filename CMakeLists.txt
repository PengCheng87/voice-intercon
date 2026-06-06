cmake_minimum_required(VERSION 3.16)
project(voice-intercom VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(BUILD_SERVER "Build SFU server" ON)
option(BUILD_CLIENT "Build voice client" ON)
option(BUILD_TESTS "Build test programs" ON)

find_package(Qt5 REQUIRED COMPONENTS Core Network)
# Override MOC to use wrapper that bypasses IPGuard encryption
set_target_properties(Qt5::moc PROPERTIES IMPORTED_LOCATION "C:/msys64/mingw64/bin/moc_wrapper.exe")
find_package(Threads REQUIRED)

# Dependencies: try system packages first, fall back to FetchContent
include(FetchContent)

# spdlog
find_package(spdlog QUIET)
if(spdlog_FOUND)
    message(STATUS "spdlog: using system package")
else()
    message(STATUS "spdlog: downloading via FetchContent")
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.14.1
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# nlohmann/json
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "nlohmann_json: using system package")
else()
    message(STATUS "nlohmann_json: downloading via FetchContent")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# yaml-cpp
find_package(yaml-cpp QUIET)
if(yaml-cpp_FOUND)
    message(STATUS "yaml-cpp: using system package")
else()
    message(STATUS "yaml-cpp: downloading via FetchContent")
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0
    )
    FetchContent_MakeAvailable(yaml-cpp)
endif()

# Build common library
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/common)
    add_subdirectory(common)
endif()

# Build server
if(BUILD_SERVER)
    add_subdirectory(server)
endif()

# Build client
if(BUILD_CLIENT)
    add_subdirectory(client)
endif()

# Build tests
if(BUILD_TESTS)
    find_package(Qt5 REQUIRED COMPONENTS Test)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test)
        add_subdirectory(test)
    endif()
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/auto-test)
        add_subdirectory(test/auto-test)
    endif()
endif()
