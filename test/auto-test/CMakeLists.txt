cmake_minimum_required(VERSION 3.16)
project(voice-auto-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

# 服务端源文件（RoomManager + SFUManager）
set(SERVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src/room_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src/sfu_manager.cpp
)

# 客户端源文件（AudioManager）
set(CLIENT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/src/audio_manager.cpp
)

set(TEST_SOURCES
    main.cpp
    ${SERVER_SOURCES}
    ${CLIENT_SOURCES}
)

set(TEST_HEADERS
    tests.h
)

add_executable(voice-auto-test ${TEST_SOURCES} ${TEST_HEADERS})

target_include_directories(voice-auto-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/src
)

target_link_libraries(voice-auto-test PRIVATE
    Qt5::Core
    Qt5::Test
    Threads::Threads
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    voice-common
)
