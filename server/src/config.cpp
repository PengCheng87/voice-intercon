#include "config.h"

#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <fstream>

namespace voice::sfu {

ServerConfig load_from_yaml(const std::string& path) {
    ServerConfig config;

    try {
        // 检查文件是否存在
        std::ifstream test(path);
        if (!test.good()) {
            throw std::runtime_error("配置文件不存在或无法读取: " + path);
        }
        test.close();

        // 解析 YAML 文件
        YAML::Node root = YAML::LoadFile(path);

        // 解析 server 节点
        if (root["server"]) {
            const auto& server = root["server"];

            if (server["signaling_port"]) {
                config.signaling_port = server["signaling_port"].as<uint16_t>();
            }
            if (server["media_port_min"]) {
                config.media_port_min = server["media_port_min"].as<uint16_t>();
            }
            if (server["media_port_max"]) {
                config.media_port_max = server["media_port_max"].as<uint16_t>();
            }
            if (server["max_participants"]) {
                config.max_participants = server["max_participants"].as<int>();
            }
        }

        // 解析 logging 节点
        if (root["logging"]) {
            const auto& logging = root["logging"];

            if (logging["level"]) {
                config.log_level = logging["level"].as<std::string>();
            }
            if (logging["file"]) {
                config.log_file = logging["file"].as<std::string>();
            }
        }

    } catch (const YAML::Exception& e) {
        throw std::runtime_error(std::string("YAML 解析错误: ") + e.what());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("加载配置失败: ") + e.what());
    }

    // 校验配置值
    if (config.signaling_port == 0) {
        throw std::runtime_error("signaling_port 不能为 0");
    }
    if (config.media_port_min > config.media_port_max) {
        throw std::runtime_error("media_port_min 不能大于 media_port_max");
    }
    if (config.max_participants <= 0) {
        throw std::runtime_error("max_participants 必须大于 0");
    }

    return config;
}

} // namespace voice::sfu
