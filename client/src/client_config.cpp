#include "client_config.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <stdexcept>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace voice::client {

// ============================================================
// PID文件路径
// ============================================================
std::string ClientConfig::pid_file_path() const {
#ifdef _WIN32
    char tmp_dir[MAX_PATH + 1];
    DWORD ret = GetTempPathA(MAX_PATH, tmp_dir);
    if (ret > 0) {
        return std::string(tmp_dir) + "voice-client-" + std::to_string(control.port) + ".pid";
    }
    // fallback
    return "voice-client-" + std::to_string(control.port) + ".pid";
#else
    return "/tmp/voice-client-" + std::to_string(control.port) + ".pid";
#endif
}

std::string ClientConfig::default_config_path() {
    return "config.yaml";
}

// ============================================================
// 从YAML加载配置
// ============================================================
void ClientConfig::load_from_yaml(const std::string& path) {
    spdlog::info("正在加载配置文件: {}", path);

    try {
        YAML::Node root = YAML::LoadFile(path);

        // 服务器配置
        if (root["server"]) {
            auto srv = root["server"];
            if (srv["url"])  server_url = srv["url"].as<std::string>();
            if (srv["room"]) room_id    = srv["room"].as<std::string>();
            if (srv["user"]) user_id    = srv["user"].as<std::string>();
            if (srv["role"]) role       = srv["role"].as<std::string>();
        }

        // 音频配置
        if (root["audio"]) {
            auto aud = root["audio"];
            if (aud["input_device"])  audio.input_device  = aud["input_device"].as<std::string>();
            if (aud["output_device"]) audio.output_device = aud["output_device"].as<std::string>();
            if (aud["sample_rate"])    audio.sample_rate   = aud["sample_rate"].as<int>();
            if (aud["channels"])      audio.channels      = aud["channels"].as<int>();
            if (aud["bitrate"])       audio.bitrate       = aud["bitrate"].as<int>();
            if (aud["aec_enabled"])   audio.aec_enabled   = aud["aec_enabled"].as<bool>();
            if (aud["aec_mode"])      audio.aec_mode      = aud["aec_mode"].as<std::string>();
        }

        // 外部音频配置
        if (root["external_audio"]) {
            auto ext = root["external_audio"];
            if (ext["enabled"])        external_audio.enabled        = ext["enabled"].as<bool>();
            if (ext["virtual_device"]) external_audio.virtual_device = ext["virtual_device"].as<std::string>();
        }

        // 日志配置
        if (root["logging"]) {
            auto log = root["logging"];
            if (log["level"]) logging.level = log["level"].as<std::string>();
            if (log["file"])  logging.file  = log["file"].as<std::string>();
        }

        // 控制配置
        if (root["control"]) {
            auto ctrl = root["control"];
            if (ctrl["port"]) control.port = ctrl["port"].as<int>();
        }

    } catch (const YAML::Exception& e) {
        spdlog::error("解析配置文件失败: {}", e.what());
        throw std::runtime_error(std::string("解析配置文件失败: ") + e.what());
    } catch (const std::exception& e) {
        spdlog::error("加载配置文件失败: {}", e.what());
        throw std::runtime_error(std::string("加载配置文件失败: ") + e.what());
    }

    spdlog::info("配置加载完成 - 服务器: {}, 房间: {}, 用户: {}, 角色: {}",
                 server_url, room_id, user_id, role);
}

// ============================================================
// 保存配置到YAML
// ============================================================
void ClientConfig::save_to_yaml(const std::string& path) const {
    spdlog::info("正在保存配置文件: {}", path);

    YAML::Emitter out;
    out << YAML::BeginMap;

    // 服务器
    out << YAML::Key << "server" << YAML::BeginMap;
    out << YAML::Key << "url"  << YAML::Value << server_url;
    out << YAML::Key << "room" << YAML::Value << room_id;
    out << YAML::Key << "user" << YAML::Value << user_id;
    out << YAML::Key << "role" << YAML::Value << role;
    out << YAML::EndMap;

    // 音频
    out << YAML::Key << "audio" << YAML::BeginMap;
    out << YAML::Key << "input_device"  << YAML::Value << audio.input_device;
    out << YAML::Key << "output_device" << YAML::Value << audio.output_device;
    out << YAML::Key << "sample_rate"   << YAML::Value << audio.sample_rate;
    out << YAML::Key << "channels"      << YAML::Value << audio.channels;
    out << YAML::Key << "bitrate"       << YAML::Value << audio.bitrate;
    out << YAML::Key << "aec_enabled"   << YAML::Value << audio.aec_enabled;
    out << YAML::Key << "aec_mode"      << YAML::Value << audio.aec_mode;
    out << YAML::EndMap;

    // 外部音频
    out << YAML::Key << "external_audio" << YAML::BeginMap;
    out << YAML::Key << "enabled"        << YAML::Value << external_audio.enabled;
    out << YAML::Key << "virtual_device" << YAML::Value << external_audio.virtual_device;
    out << YAML::EndMap;

    // 日志
    out << YAML::Key << "logging" << YAML::BeginMap;
    out << YAML::Key << "level" << YAML::Value << logging.level;
    out << YAML::Key << "file"  << YAML::Value << logging.file;
    out << YAML::EndMap;

    // 控制
    out << YAML::Key << "control" << YAML::BeginMap;
    out << YAML::Key << "port" << YAML::Value << control.port;
    out << YAML::EndMap;

    out << YAML::EndMap;

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        throw std::runtime_error("无法打开配置文件进行写入: " + path);
    }
    ofs << out.c_str() << std::endl;
    ofs.close();

    spdlog::info("配置文件已保存: {}", path);
}

} // namespace voice::client
