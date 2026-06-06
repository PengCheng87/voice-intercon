/**
 * voice-client - 语音对讲客户端主入口（Qt5版本）
 *
 * 支持子命令模式:
 *   voice-client start --server <host> --port <port> --room <room_id> --user <user_id> [--role doctor|host] [--config <path>]
 *   voice-client stop
 *   voice-client mute
 *   voice-client unmute
 *   voice-client silence
 *   voice-client listen
 *   voice-client status
 *   voice-client list
 *   voice-client config --set <key>=<value>
 *   voice-client config --show
 */

#include "client_config.h"
#include "signaling_client.h"
#include "audio_manager.h"
#include "control_server.h"

#include <QCoreApplication>
#include <QHostInfo>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <iostream>
#include <string>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ============================================================
// 跨平台进程工具函数
// ============================================================

/// 获取当前进程ID
static int current_process_id() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

/// 检查进程是否存在
static bool process_exists(int pid) {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (hProcess == NULL) return false;
    DWORD exit_code;
    bool alive = (GetExitCodeProcess(hProcess, &exit_code) && exit_code == STILL_ACTIVE);
    CloseHandle(hProcess);
    return alive;
#else
    return kill(pid, 0) == 0;
#endif
}

/// 终止进程
static bool terminate_process(int pid, bool force = false) {
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (hProcess == NULL) return false;
    bool ok = (TerminateProcess(hProcess, 1) != 0);
    CloseHandle(hProcess);
    return ok;
#else
    if (force) {
        return kill(pid, SIGKILL) == 0;
    } else {
        return kill(pid, SIGTERM) == 0;
}
#endif
}

// ============================================================
// 全局状态（用于信号处理）
// ============================================================
static QCoreApplication* g_app = nullptr;
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_need_stop{false};

/// 信号处理函数
static void signal_handler(int sig) {
    spdlog::info("收到信号 {}，准备退出...", sig);
    g_running = false;
    g_need_stop = true;
    if (g_app) {
        g_app->quit();
    }
}

// ============================================================
// PID文件管理
// ============================================================
/// 写入PID文件
static bool write_pid_file(const std::string& path) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        spdlog::error("无法写入PID文件: {}", path);
        return false;
    }
    ofs << current_process_id() << std::endl;
    ofs.close();
    spdlog::debug("PID文件已写入: {} (PID={})", path, getpid());
    return true;
}

/// 删除PID文件
static void remove_pid_file(const std::string& path) {
    if (std::remove(path.c_str()) == 0) {
        spdlog::debug("PID文件已删除: {}", path);
    }
}

/// 读取PID文件中的PID
static int read_pid_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return -1;
    int pid = -1;
    ifs >> pid;
    return pid;
}

// ============================================================
// 初始化日志系统
// ============================================================
static void init_logging(const voice::client::LoggingConfig& log_config) {
    std::vector<spdlog::sink_ptr> sinks;

    // 控制台输出
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [voice-client] %v");
    sinks.push_back(console_sink);

    // 文件输出
    if (!log_config.file.empty()) {
        try {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_config.file, true);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [voice-client] %v");
            sinks.push_back(file_sink);
        } catch (const std::exception& e) {
            std::cerr << "无法创建日志文件: " << e.what() << std::endl;
        }
    }

    auto logger = std::make_shared<spdlog::logger>("voice-client", sinks.begin(), sinks.end());

    // 设置日志级别
    std::string level = log_config.level;
    std::transform(level.begin(), level.end(), level.begin(), ::tolower);
    if (level == "trace")        logger->set_level(spdlog::level::trace);
    else if (level == "debug")   logger->set_level(spdlog::level::debug);
    else if (level == "info")    logger->set_level(spdlog::level::info);
    else if (level == "warn" || level == "warning") logger->set_level(spdlog::level::warn);
    else if (level == "error")   logger->set_level(spdlog::level::err);
    else if (level == "critical") logger->set_level(spdlog::level::critical);
    else logger->set_level(spdlog::level::info);

    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
}

// ============================================================
// 帮助信息
// ============================================================
static void print_usage(const char* program) {
    std::cout << "语音对讲客户端 v1.0.0 (Qt5 DTLS)" << std::endl;
    std::cout << std::endl;
    std::cout << "用法:" << std::endl;
    std::cout << "  " << program << " <命令> [选项]" << std::endl;
    std::cout << std::endl;
    std::cout << "命令:" << std::endl;
    std::cout << "  start     启动客户端并连接服务器" << std::endl;
    std::cout << "  stop      停止运行中的客户端" << std::endl;
    std::cout << "  mute      静音（停止发送音频）" << std::endl;
    std::cout << "  unmute    取消静音" << std::endl;
    std::cout << "  silence   停止收听（不播放接收到的音频）" << std::endl;
    std::cout << "  listen    开始收听" << std::endl;
    std::cout << "  status    查看当前状态" << std::endl;
    std::cout << "  list      列出房间参与者" << std::endl;
    std::cout << "  config    配置管理" << std::endl;
    std::cout << std::endl;
    std::cout << "start命令选项:" << std::endl;
    std::cout << "  --server <host>   信令服务器主机 (默认: localhost)" << std::endl;
    std::cout << "  --port <port>     信令服务器端口 (默认: 8443)" << std::endl;
    std::cout << "  --room <id>       房间ID" << std::endl;
    std::cout << "  --user <id>       用户ID" << std::endl;
    std::cout << "  --role <role>     角色 (doctor/host, 默认: doctor)" << std::endl;
    std::cout << "  --config <path>   配置文件路径" << std::endl;
    std::cout << "  --no-verify       跳过DTLS证书验证（开发模式）" << std::endl;
    std::cout << std::endl;
    std::cout << "config命令选项:" << std::endl;
    std::cout << "  --set <key>=<value>  设置配置项" << std::endl;
    std::cout << "  --show               显示当前配置" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " start --server 192.168.1.100 --port 8443 --room surgery_001 --user doctor_a --role doctor" << std::endl;
    std::cout << "  " << program << " start --config config-doctor.yaml" << std::endl;
    std::cout << "  " << program << " mute" << std::endl;
    std::cout << "  " << program << " status" << std::endl;
    std::cout << "  " << program << " config --set server.url=ws://192.168.1.100:8080" << std::endl;
}

// ============================================================
// 解析命令行参数
// ============================================================
struct ParsedArgs {
    std::string command;      // 子命令
    std::string server;       // --server
    int         port = 8443;  // --port (DTLS默认端口)
    std::string room;         // --room
    std::string user;         // --user
    std::string role;         // --role
    std::string config_path;  // --config
    bool        no_verify = false; // --no-verify
    std::string config_set;   // --set (config子命令)
    bool        config_show   = false;  // --show (config子命令)
};

static ParsedArgs parse_args(int argc, char* argv[]) {
    ParsedArgs args;

    if (argc < 2) {
        args.command = "help";
        return args;
    }

    args.command = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--server" && i + 1 < argc) {
            args.server = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            try {
                args.port = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "端口号格式错误" << std::endl;
            }
        } else if (arg == "--room" && i + 1 < argc) {
            args.room = argv[++i];
        } else if (arg == "--user" && i + 1 < argc) {
            args.user = argv[++i];
        } else if (arg == "--role" && i + 1 < argc) {
            args.role = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            args.config_path = argv[++i];
        } else if (arg == "--no-verify") {
            args.no_verify = true;
        } else if (arg == "--set" && i + 1 < argc) {
            args.config_set = argv[++i];
        } else if (arg == "--show") {
            args.config_show = true;
        } else if (arg == "--help" || arg == "-h") {
            args.command = "help";
        } else {
            std::cerr << "未知参数: " << arg << std::endl;
        }
    }

    return args;
}

// ============================================================
// 配置管理命令
// ============================================================
static int cmd_config(const ParsedArgs& args) {
    // 确定配置文件路径
    std::string config_path = args.config_path;
    if (config_path.empty()) {
        config_path = voice::client::ClientConfig::default_config_path();
    }

    voice::client::ClientConfig config;

    // 如果文件存在，先加载
    std::ifstream check(config_path);
    if (check.good()) {
        check.close();
        try {
            config.load_from_yaml(config_path);
        } catch (const std::exception& e) {
            std::cerr << "加载配置失败: " << e.what() << std::endl;
            return 1;
        }
    }

    // --show: 显示当前配置
    if (args.config_show) {
        std::cout << "当前配置 (" << config_path << "):" << std::endl;
        std::cout << "  服务器URL: " << config.server_url << std::endl;
        std::cout << "  房间ID:    " << config.room_id << std::endl;
        std::cout << "  用户ID:    " << config.user_id << std::endl;
        std::cout << "  角色:      " << config.role << std::endl;
        std::cout << "  输入设备:  " << config.audio.input_device << std::endl;
        std::cout << "  输出设备:  " << config.audio.output_device << std::endl;
        std::cout << "  采样率:    " << config.audio.sample_rate << std::endl;
        std::cout << "  声道数:    " << config.audio.channels << std::endl;
        std::cout << "  比特率:    " << config.audio.bitrate << std::endl;
        std::cout << "  回声消除:  " << (config.audio.aec_enabled ? "开启" : "关闭") << std::endl;
        std::cout << "  AEC模式:   " << config.audio.aec_mode << std::endl;
        std::cout << "  外部音频:  " << (config.external_audio.enabled ? "开启" : "关闭") << std::endl;
        std::cout << "  虚拟设备:  " << config.external_audio.virtual_device << std::endl;
        std::cout << "  日志级别:  " << config.logging.level << std::endl;
        std::cout << "  日志文件:  " << config.logging.file << std::endl;
        std::cout << "  控制端口:  " << config.control.port << std::endl;
        return 0;
    }

    // --set: 设置配置项
    if (!args.config_set.empty()) {
        std::size_t eq_pos = args.config_set.find('=');
        if (eq_pos == std::string::npos) {
            std::cerr << "配置格式错误，应为 key=value" << std::endl;
            return 1;
        }

        std::string key = args.config_set.substr(0, eq_pos);
        std::string value = args.config_set.substr(eq_pos + 1);

        // 解析嵌套键 (如 server.url)
        if (key == "server.url")       config.server_url = value;
        else if (key == "server.room") config.room_id = value;
        else if (key == "server.user") config.user_id = value;
        else if (key == "server.role") config.role = value;
        else if (key == "audio.input_device")  config.audio.input_device = value;
        else if (key == "audio.output_device") config.audio.output_device = value;
        else if (key == "audio.sample_rate")   config.audio.sample_rate = std::stoi(value);
        else if (key == "audio.channels")     config.audio.channels = std::stoi(value);
        else if (key == "audio.bitrate")      config.audio.bitrate = std::stoi(value);
        else if (key == "audio.aec_enabled")  config.audio.aec_enabled = (value == "true" || value == "1");
        else if (key == "audio.aec_mode")     config.audio.aec_mode = value;
        else if (key == "external_audio.enabled")        config.external_audio.enabled = (value == "true" || value == "1");
        else if (key == "external_audio.virtual_device") config.external_audio.virtual_device = value;
        else if (key == "logging.level") config.logging.level = value;
        else if (key == "logging.file")  config.logging.file = value;
        else if (key == "control.port")  config.control.port = std::stoi(value);
        else {
            std::cerr << "未知配置项: " << key << std::endl;
            return 1;
        }

        try {
            config.save_to_yaml(config_path);
            std::cout << "配置已保存到 " << config_path << std::endl;
            std::cout << "  " << key << " = " << value << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "保存配置失败: " << e.what() << std::endl;
            return 1;
        }
        return 0;
    }

    // 没有指定 --show 或 --set
    std::cerr << "请指定 --show 或 --set <key>=<value>" << std::endl;
    return 1;
}

// ============================================================
// 通过本地控制端口发送命令
// ============================================================
static int send_control_command(const std::string& command, int port) {
    std::string response = voice::client::ControlServer::send_command(port, command);

    // 解析响应
    using json = nlohmann::json;
    json root;
    try {
        root = json::parse(response);
    } catch (const json::parse_error& e) {
        std::cerr << "解析响应失败: " << response << std::endl;
        return 1;
    }

    if (root["success"].get<bool>()) {
        // 输出结果
        if (root.contains("data")) {
            std::cout << root["data"].dump(2) << std::endl;
        } else if (root.contains("message")) {
            std::cout << root["message"].get<std::string>() << std::endl;
        }
        return 0;
    } else {
        std::cerr << "错误: " << root["error"].get<std::string>() << std::endl;
        return 1;
    }
}

// ============================================================
// 运行中的客户端状态
// ============================================================
struct ClientState {
    voice::client::ClientConfig      config;
    voice::client::SignalingClient*  signaling = nullptr;
    voice::client::AudioManager      audio;
    voice::client::ControlServer     control;
    std::mutex                       state_mutex;
    bool                             joined = false;
};

// ============================================================
// 处理控制命令
// ============================================================
static std::string handle_control_command(ClientState* state,
                                           const std::string& command,
                                           const std::string& /*params*/) {
    nlohmann::json resp;
    resp["success"] = true;

    if (command == "stop") {
        spdlog::info("收到停止命令");
        g_running = false;
        g_need_stop = true;
        if (g_app) {
            g_app->quit();
        }
        resp["message"] = "正在停止...";

    } else if (command == "mute") {
        state->audio.set_mute(true);
        if (state->signaling) {
            state->signaling->send_mute(true);
        }
        resp["message"] = "已静音";
        resp["data"]["muted"] = true;

    } else if (command == "unmute") {
        state->audio.set_mute(false);
        if (state->signaling) {
            state->signaling->send_mute(false);
        }
        resp["message"] = "已取消静音";
        resp["data"]["muted"] = false;

    } else if (command == "silence") {
        state->audio.set_silence(true);
        resp["message"] = "已停止收听";
        resp["data"]["silenced"] = true;

    } else if (command == "listen") {
        state->audio.set_silence(false);
        resp["message"] = "已恢复收听";
        resp["data"]["silenced"] = false;

    } else if (command == "status") {
        resp["data"]["connected"]    = state->signaling ? state->signaling->is_connected() : false;
        resp["data"]["joined"]       = state->joined;
        resp["data"]["muted"]        = state->audio.is_muted();
        resp["data"]["silenced"]     = state->audio.is_silenced();
        resp["data"]["capturing"]    = state->audio.is_capturing();
        resp["data"]["playing"]     = state->audio.is_playing();
        resp["data"]["audio_level"]  = state->audio.get_audio_level();
        resp["data"]["server"]      = state->config.server_url;
        resp["data"]["room"]        = state->config.room_id;
        resp["data"]["user"]        = state->config.user_id;
        resp["data"]["role"]        = state->config.role;

    } else if (command == "list") {
        auto participants = state->signaling ? state->signaling->get_participants() : std::vector<voice::client::Participant>{};
        nlohmann::json parts_array = nlohmann::json::array();
        for (const auto& p : participants) {
            nlohmann::json item;
            item["user_id"] = p.user_id;
            item["role"]    = p.role;
            item["muted"]   = p.muted;
            parts_array.push_back(item);
        }
        resp["data"]["participants"] = parts_array;
        resp["data"]["count"]        = static_cast<int>(participants.size());

    } else {
        resp["success"] = false;
        resp["error"]   = "未知命令: " + command;
    }

    return resp.dump();
}

// ============================================================
// start命令 - 主运行逻辑
// ============================================================
static int cmd_start(int argc, char* argv[], const ParsedArgs& args) {
    voice::client::ClientConfig config;

    // 加载配置文件（如果指定）
    if (!args.config_path.empty()) {
        try {
            config.load_from_yaml(args.config_path);
        } catch (const std::exception& e) {
            std::cerr << "加载配置文件失败: " << e.what() << std::endl;
            return 1;
        }
    }

    // 命令行参数覆盖配置文件
    if (!args.server.empty()) config.server_url = args.server;
    if (args.port != 8443)    {
        // 将端口信息存入server_url以便后续解析
        config.server_url = args.server + ":" + std::to_string(args.port);
    }
    if (!args.room.empty())   config.room_id    = args.room;
    if (!args.user.empty())   config.user_id    = args.user;
    if (!args.role.empty())   config.role       = args.role;

    // 验证必要参数
    if (config.server_url.empty()) {
        std::cerr << "错误: 未指定服务器 (--server 或配置文件)" << std::endl;
        return 1;
    }
    if (config.room_id.empty()) {
        std::cerr << "错误: 未指定房间ID (--room 或配置文件)" << std::endl;
        return 1;
    }
    if (config.user_id.empty()) {
        std::cerr << "错误: 未指定用户ID (--user 或配置文件)" << std::endl;
        return 1;
    }

    // 初始化日志
    init_logging(config.logging);

    // 检查PID文件（防止重复启动）
    std::string pid_path = config.pid_file_path();
    int existing_pid = read_pid_file(pid_path);
    if (existing_pid > 0) {
        // 检查进程是否存在
        if (process_exists(existing_pid)) {
            spdlog::error("voice-client已在运行 (PID={})，请先停止", existing_pid);
            std::cerr << "voice-client已在运行 (PID=" << existing_pid << ")，请先使用 voice-client stop 停止" << std::endl;
            return 1;
        } else {
            spdlog::warn("PID文件存在但进程 {} 已不存在，清理旧PID文件", existing_pid);
            remove_pid_file(pid_path);
        }
    }

    // 写入PID文件
    if (!write_pid_file(pid_path)) {
        return 1;
    }

    // 注册信号处理
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    spdlog::info("========================================");
    spdlog::info("语音对讲客户端 v1.0.0 启动 (Qt5 DTLS)");
    spdlog::info("========================================");
    spdlog::info("服务器: {}", config.server_url);
    spdlog::info("房间:   {}", config.room_id);
    spdlog::info("用户:   {}", config.user_id);
    spdlog::info("角色:   {}", config.role);

    // 创建Qt应用（用于start命令）
    int qt_argc = 1;
    char* qt_argv[] = { argv[0], nullptr };
    QCoreApplication app(qt_argc, qt_argv);
    g_app = &app;

    // 创建客户端状态
    ClientState state;
    state.config = config;

    // 启动本地控制服务器
    state.control.set_command_handler([&state](const std::string& cmd, const std::string& params) {
        return handle_control_command(&state, cmd, params);
    });

    if (!state.control.start(config.control.port)) {
        spdlog::error("启动控制服务器失败: {}", state.control.get_last_error());
        std::cerr << "启动控制服务器失败: " << state.control.get_last_error() << std::endl;
        remove_pid_file(pid_path);
        return 1;
    }

    // 初始化音频管理器
    if (!state.audio.init(config.audio.input_device,
                          config.audio.output_device,
                          config.audio.sample_rate,
                          config.audio.channels)) {
        spdlog::error("初始化音频管理器失败");
        remove_pid_file(pid_path);
        return 1;
    }

    // 创建信令客户端
    state.signaling = new voice::client::SignalingClient(&app);

    // 连接信令客户端信号
    QObject::connect(state.signaling, &voice::client::SignalingClient::connected, [&state]() {
        spdlog::info("已连接到信令服务器");
    });

    QObject::connect(state.signaling, &voice::client::SignalingClient::disconnected, [&state](const QString& reason) {
        spdlog::warn("信令连接断开: {}", reason.toStdString());
        state.joined = false;
    });

    QObject::connect(state.signaling, &voice::client::SignalingClient::joinAccepted, [&state](const std::vector<voice::client::Participant>& participants) {
        spdlog::info("加入房间成功，参与者数量: {}", participants.size());
        state.joined = true;

        // 开始音频采集和播放
        state.audio.start_capture();
        state.audio.start_playback();
    });

    QObject::connect(state.signaling, &voice::client::SignalingClient::participantUpdate, [](const voice::client::Participant& participant, bool joined) {
        if (joined) {
            spdlog::info("参与者加入: {} ({})", participant.user_id, participant.role);
        } else {
            spdlog::info("参与者离开: {}", participant.user_id);
        }
    });

    QObject::connect(state.signaling, &voice::client::SignalingClient::audioToggle, [](const QString& user_id, bool muted) {
        spdlog::info("用户 {} 静音状态: {}", user_id.toStdString(), muted ? "是" : "否");
    });

    QObject::connect(state.signaling, &voice::client::SignalingClient::tonePlayed, [](const QString& tone_id, const QString& user_id) {
        spdlog::info("提示音: {} 来自 {}", tone_id.toStdString(), user_id.toStdString());
        // TODO: 播放提示音
    });

    // 设置音频采集回调
    state.audio.set_capture_callback([&state](const std::int16_t* data, std::size_t frames) {
        if (!state.joined || !state.signaling || !state.signaling->is_connected()) return;

        // TODO: 编码并发送音频数据
        // auto encoded = state.audio.encode_audio(data, frames);
        // state.signaling->send_audio(encoded);
        (void)data;
        (void)frames;
    });

    // 解析服务器地址和端口
    QString server_host = QString::fromStdString(config.server_url);
    quint16 server_port = 8443; // DTLS默认端口

    // 如果server_url包含端口号，解析出来
    int colon_pos = server_host.lastIndexOf(':');
    if (colon_pos > 0) {
        bool ok = false;
        quint16 parsed_port = server_host.mid(colon_pos + 1).toUShort(&ok);
        if (ok) {
            server_port = parsed_port;
            server_host = server_host.left(colon_pos);
        }
    }

    // 移除可能的协议前缀（兼容旧配置）
    if (server_host.startsWith("ws://")) {
        server_host = server_host.mid(5);
    } else if (server_host.startsWith("wss://")) {
        server_host = server_host.mid(6);
    }

    // 连接信令服务器
    spdlog::info("正在连接DTLS信令服务器 {}:{} ...", server_host.toStdString(), server_port);
    if (!state.signaling->connect_to_server(server_host, server_port, !args.no_verify)) {
        spdlog::error("连接信令服务器失败: {}", state.signaling->get_last_error());
        std::cerr << "连接信令服务器失败: " << state.signaling->get_last_error() << std::endl;
        state.audio.cleanup();
        state.control.stop();
        remove_pid_file(pid_path);
        return 1;
    }

    // 等待DTLS握手完成后再发送加入请求
    // 使用Qt信号槽机制，在connected信号中发送join
    QObject::connect(state.signaling, &voice::client::SignalingClient::connected, [&state, &config]() {
        // 发送加入房间请求
        if (!state.signaling->send_join(config.room_id, config.user_id, config.role)) {
            spdlog::error("发送加入房间请求失败");
            if (g_app) {
                g_app->quit();
            }
        }
    });

    // 进入Qt事件循环
    spdlog::info("客户端运行中，按 Ctrl+C 退出");
    int ret = app.exec();

    // 清理退出
    spdlog::info("正在清理资源...");

    // 离开房间
    if (state.joined && state.signaling) {
        state.signaling->send_leave();
    }

    // 停止音频
    state.audio.stop_capture();
    state.audio.stop_playback();
    state.audio.cleanup();

    // 断开信令连接
    if (state.signaling) {
        state.signaling->disconnect_from_server();
        delete state.signaling;
        state.signaling = nullptr;
    }

    // 停止控制服务器
    state.control.stop();

    // 删除PID文件
    remove_pid_file(pid_path);

    spdlog::info("客户端已退出");
    return ret;
}

// ============================================================
// 主函数
// ============================================================
int main(int argc, char* argv[]) {
    ParsedArgs args = parse_args(argc, argv);

    // 帮助
    if (args.command == "help" || args.command.empty()) {
        print_usage(argv[0]);
        return 0;
    }

    // 配置命令（不需要连接运行中的进程）
    if (args.command == "config") {
        return cmd_config(args);
    }

    // start命令（启动新进程，创建QCoreApplication）
    if (args.command == "start") {
        return cmd_start(argc, argv, args);
    }

    // 其他命令：通过本地控制端口发送给运行中的进程
    // 首先尝试从默认PID文件获取端口，或使用默认端口9090
    int control_port = 9090;

    // 如果指定了配置文件，尝试从中读取控制端口
    if (!args.config_path.empty()) {
        std::ifstream check(args.config_path);
        if (check.good()) {
            check.close();
            try {
                voice::client::ClientConfig tmp_config;
                tmp_config.load_from_yaml(args.config_path);
                control_port = tmp_config.control.port;
            } catch (...) {
                // 忽略，使用默认端口
            }
        }
    }

    // stop命令：先尝试通过控制端口发送，再尝试信号
    if (args.command == "stop") {
        // 先尝试控制端口
        std::string response = voice::client::ControlServer::send_command(control_port, "stop");
        nlohmann::json root;
        try {
            root = nlohmann::json::parse(response);
        } catch (...) {
            // JSON解析失败，继续尝试其他方式
        }
        if (root.contains("success") && root["success"].get<bool>()) {
            std::cout << root["message"].get<std::string>() << std::endl;
            return 0;
        }

        // 控制端口失败，尝试PID文件 + 进程终止
        auto fallback_pid_path = [](int port) -> std::string {
#ifdef _WIN32
            char tmp_dir[MAX_PATH + 1];
            DWORD ret = GetTempPathA(MAX_PATH, tmp_dir);
            if (ret > 0) return std::string(tmp_dir) + "voice-client-" + std::to_string(port) + ".pid";
            return "voice-client-" + std::to_string(port) + ".pid";
#else
            return "/tmp/voice-client-" + std::to_string(port) + ".pid";
#endif
        };
        std::string pid_path = fallback_pid_path(control_port);
        int pid = read_pid_file(pid_path);
        if (pid > 0 && process_exists(pid)) {
            std::cout << "正在发送停止信号到进程 " << pid << "..." << std::endl;
            terminate_process(pid, false);
            // 等待进程退出
            for (int i = 0; i < 50; ++i) {  // 5秒超时
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!process_exists(pid)) {
                    std::cout << "进程已停止" << std::endl;
                    remove_pid_file(pid_path);
                    return 0;
                }
            }
            std::cerr << "进程未能在5秒内退出，发送强制终止..." << std::endl;
            terminate_process(pid, true);
            remove_pid_file(pid_path);
            return 0;
        }

        std::cerr << "未找到运行中的voice-client进程" << std::endl;
        return 1;
    }

    // mute/unmute/silence/listen/status/list 命令
    return send_control_command(args.command, control_port);
}
