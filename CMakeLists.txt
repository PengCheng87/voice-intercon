cmake_minimum_required(VERSION 3.16)
project(voice-intercom VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt MOC支持
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 选项：是否编译服务端和客户端
option(BUILD_SERVER "编译SFU服务端" ON)
option(BUILD_CLIENT "编译语音客户端" ON)
option(BUILD_TESTS "编译测试程序" ON)

# 查找Qt5依赖
find_package(Qt5 REQUIRED COMPONENTS Core Network)
find_package(Threads REQUIRED)

# FetchContent 下载第三方库
include(FetchContent)

# spdlog - 日志库
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
)

# nlohmann/json - JSON库 (header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

# yaml-cpp - YAML解析库
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
)

FetchContent_MakeAvailable(spdlog nlohmann_json yaml-cpp)

# 先编译公共库
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/common)
    add_subdirectory(common)
endif()

# 编译服务端
if(BUILD_SERVER)
    add_subdirectory(server)
endif()

# 编译客户端
if(BUILD_CLIENT)
    add_subdirectory(client)
endif()

# 编译测试程序
if(BUILD_TESTS)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test)
        add_subdirectory(test)
    endif()
    # 自动化测试（Qt Test, 无GUI依赖）
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/auto-test)
        add_subdirectory(test/auto-test)
    endif()
endif()
