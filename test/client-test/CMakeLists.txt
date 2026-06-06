cmake_minimum_required(VERSION 3.16)
project(voice-client-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 客户端源文件（测试需要编译被测对象）
set(CLIENT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/src/signaling_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/src/audio_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/src/control_server.cpp
)

# 源文件
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    ${CLIENT_SOURCES}
)

set(HEADERS
    src/mainwindow.h
)

add_executable(voice-client-test ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(voice-client-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/src
)

# 链接库
target_link_libraries(voice-client-test PRIVATE
    Qt5::Core
    Qt5::Network
    Qt5::Widgets
    Threads::Threads
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    voice-common
)
