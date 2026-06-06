cmake_minimum_required(VERSION 3.16)
project(voice-intercom-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt MOC 支持
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 查找 Qt5 依赖
find_package(Qt5 REQUIRED COMPONENTS Core Network Widgets)
find_package(Threads REQUIRED)

# 添加 tone service 源文件
set(TONE_SERVICE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/src/tone_service.cpp
)
set(TONE_SERVICE_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/src
)

# 添加服务端测试程序
add_subdirectory(server-test)

# 添加客户端测试程序
add_subdirectory(client-test)
