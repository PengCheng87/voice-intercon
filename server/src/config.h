#pragma once

#include <string>
#include <cstdint>

namespace voice::sfu {

/// 服务端配置结构体
struct ServerConfig {
    // 服务器配置
    uint16_t signaling_port = 8080;       ///< 信令服务器端口
    uint16_t media_port_min  = 4000;      ///< 媒体端口范围最小值
    uint16_t media_port_max  = 4010;      ///< 媒体端口范围最大值
    int      max_participants = 16;       ///< 每个房间最大参与者数量

    // 日志配置
    std::string log_level = "info";       ///< 日志级别：trace, debug, info, warn, error, critical
    std::string log_file  = "voice-sfu-server.log"; ///< 日志文件路径
};

/// 从 YAML 文件加载配置
/// @param path YAML 配置文件路径
/// @return 解析后的 ServerConfig
/// @throws std::runtime_error 如果文件无法读取或解析失败
ServerConfig load_from_yaml(const std::string& path);

} // namespace voice::sfu
