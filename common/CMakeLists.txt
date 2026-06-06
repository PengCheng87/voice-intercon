# common/CMakeLists.txt - 公共库

cmake_minimum_required(VERSION 3.14)

# 创建静态库 voice-common
add_library(voice-common STATIC
    opus_codec.cpp
    priority_mixer.cpp
    media_stats.cpp
    dtls_srtp.cpp
)

target_include_directories(voice-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 查找Opus库（支持vcpkg / pkg-config / 手动查找）
# 方式1: CMake config（vcpkg等提供）
find_package(opus QUIET CONFIG)

if(opus_FOUND)
    message(STATUS "Opus: 通过CMake config找到 (vcpkg)")
    target_link_libraries(voice-common PUBLIC opus::opus)
else()
    # 方式2: pkg-config（Linux/macOS）
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPUS opus)
    endif()

    # 方式3: 手动搜索头文件和库
    if(NOT OPUS_FOUND)
        find_path(OPUS_INCLUDE_DIRS opus/opus.h
            HINTS
                ENV OPUS_ROOT
                ${OPUS_ROOT}
            PATHS
                /usr/include /usr/local/include
                /usr/local/opt/opus/include          # macOS Homebrew
                "C:/Program Files/opus/include"
                "C:/vcpkg/installed/x64-windows/include"
                "C:/vcpkg/installed/x86-windows/include"
        )
        find_library(OPUS_LIBRARIES
            NAMES opus libopus
            HINTS
                ENV OPUS_ROOT
                ${OPUS_ROOT}
            PATHS
                /usr/lib /usr/local/lib
                /usr/local/opt/opus/lib
                "C:/Program Files/opus/lib"
                "C:/vcpkg/installed/x64-windows/lib"
                "C:/vcpkg/installed/x86-windows/lib"
        )
        if(OPUS_INCLUDE_DIRS AND OPUS_LIBRARIES)
            set(OPUS_FOUND TRUE)
        endif()
    endif()

    if(NOT OPUS_FOUND)
        message(FATAL_ERROR
            "未找到Opus库，请安装:\n"
            "  Linux: sudo apt install libopus-dev\n"
            "  macOS: brew install opus\n"
            "  Windows: vcpkg install opus 或设置 -DOPUS_ROOT=...\n"
        )
    endif()

    target_include_directories(voice-common PUBLIC ${OPUS_INCLUDE_DIRS})
    target_link_libraries(voice-common PUBLIC ${OPUS_LIBRARIES})
endif()

# 依赖spdlog（用于opus_codec.cpp中的日志）
target_link_libraries(voice-common PUBLIC spdlog::spdlog)
