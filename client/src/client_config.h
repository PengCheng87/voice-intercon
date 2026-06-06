#pragma once

#include <string>
#include <cstdint>

namespace voice::client {

/// 客户端音频配置
struct AudioConfig {
    std::string input_device  = "default";   ///< 输入设备名称
    std::string output_device = "default";   ///< 输出设备名称
    int         sample_rate    = 48000;       ///< 采样率
    int         channels       = 1;           ///< 声道数
    int         bitrate        = 64000;       ///< 比特率
    bool        aec_enabled    = true;        ///< 是否启用回声消除
    std::string aec_mode       = "standard";  ///< 回声消除模式: standard / aggressive
};

/// 外部音频配置（用于患者端虚拟音频设备）
struct ExternalAudioConfig {
    bool        enabled        = false;                ///< 是否启用外部音频
    std::string virtual_device = "Virtual Audio Cable"; ///< 虚拟音频设备名称
};

/// 日志配置
struct LoggingConfig {
    std::string level = "info";             ///< 日志级别: trace/debug/info/warn/error/critical
    std::string file  = "voice-client.log";  ///< 日志文件路径
};

/// 控制服务器配置
struct ControlConfig {
    int port = 9090;  ///< 本地控制端口
};

/// 客户端完整配置
struct ClientConfig {
    std::string          server_url;                 ///< 信令服务器URL (ws://host:port)
    std::string          room_id;                     ///< 房间ID
    std::string          user_id;                     ///< 用户ID
    std::string          role        = "doctor";      ///< 角色: doctor / host
    AudioConfig          audio;                       ///< 音频配置
    ExternalAudioConfig  external_audio;              ///< 外部音频配置
    LoggingConfig        logging;                     ///< 日志配置
    ControlConfig        control;                     ///< 控制服务器配置

    /// 从YAML文件加载配置，失败时抛出异常
    void load_from_yaml(const std::string& path);

    /// 保存配置到YAML文件
    void save_to_yaml(const std::string& path) const;

    /// 生成PID文件路径（基于控制端口）
    std::string pid_file_path() const;

    /// 生成默认配置文件名
    static std::string default_config_path();
};

} // namespace voice::client
