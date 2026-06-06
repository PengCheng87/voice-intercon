cmake_minimum_required(VERSION 3.14)
project(voice-client VERSION 1.0.0 LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 启用Qt MOC
set(CMAKE_AUTOMOC ON)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ============ 依赖查找 ============
find_package(Qt5 REQUIRED COMPONENTS Core Network)

# ============ 源文件 ============
set(SOURCES
    src/main.cpp
    src/client_config.cpp
    src/signaling_client.cpp
    src/audio_manager.cpp
    src/control_server.cpp
)

set(HEADERS
    src/client_config.h
    src/signaling_client.h
    src/audio_manager.h
    src/control_server.h
)

# ============ 可执行文件 ============
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Network
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    spdlog::spdlog
    Threads::Threads
)

# 如果voice-common已构建，链接之（提供Opus编解码等公共能力）
if(TARGET voice-common)
    target_link_libraries(${PROJECT_NAME} PRIVATE voice-common)
endif()

# 编译选项
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# 链接选项（Linux下需要）
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE rt)
endif()
