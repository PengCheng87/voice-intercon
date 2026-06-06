cmake_minimum_required(VERSION 3.14)
project(voice-sfu-server LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 启用 Qt MOC
set(CMAKE_AUTOMOC ON)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ========== 依赖查找 ==========
find_package(Qt5 REQUIRED COMPONENTS Core Network)

# ========== 源文件 ==========
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/signaling_server.cpp
    src/room_manager.cpp
    src/sfu_manager.cpp
    src/tone_service.cpp
)

set(HEADERS
    src/config.h
    src/signaling_server.h
    src/room_manager.h
    src/sfu_manager.h
    src/tone_service.h
)

# ========== 可执行文件 ==========
add_executable(voice-sfu-server ${SOURCES} ${HEADERS})

target_include_directories(voice-sfu-server PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(voice-sfu-server PRIVATE
    Qt5::Core
    Qt5::Network
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    Threads::Threads
    spdlog::spdlog
    voice-common
)

# 编译选项
target_compile_options(voice-sfu-server PRIVATE
    -Wall -Wextra -Wpedantic
)
