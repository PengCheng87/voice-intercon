cmake_minimum_required(VERSION 3.16)
project(voice-sfu-server-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 服务端源文件（测试需要编译被测对象）
set(SERVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src/room_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src/sfu_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src/signaling_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src/tone_service.cpp
)

# 源文件
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    ${SERVER_SOURCES}
)

set(HEADERS
    src/mainwindow.h
)

add_executable(voice-sfu-server-test ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(voice-sfu-server-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src
)

# 链接库
target_link_libraries(voice-sfu-server-test PRIVATE
    Qt5::Core
    Qt5::Network
    Qt5::Widgets
    Threads::Threads
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    voice-common
)
