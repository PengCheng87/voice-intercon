#include <QCoreApplication>
#include <QObject>
#include <QTimer>
#include <csignal>
#include <iostream>

#ifndef _WIN32
#include <signal.h>
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "config.h"
#include "signaling_server.h"
#include "room_manager.h"
#include "sfu_manager.h"

namespace {

// 全局 QCoreApplication 指针，用于信号处理中安全退出
static QCoreApplication* g_app = nullptr;

// 全局 SignalingServer 指针，用于优雅关闭
static voice::sfu::SignalingServer* g_signaling_server = nullptr;

} // anonymous namespace

// ========== 信号处理（跨平台） ==========

/// 信号处理函数（SIGINT/SIGTERM）
/// 通过 QTimer::singleShot 在主事件循环线程中调用 quit，确保线程安全
void signal_handler(int signum) {
    spdlog::info("收到信号 {}，准备优雅退出...", signum);

    if (g_signaling_server) {
        g_signaling_server->stop();
    }

    if (g_app) {
        QTimer::singleShot(0, g_app, &QCoreApplication::quit);
    }
}

/// 注册信号处理
void setup_signal_handlers() {
#ifdef _WIN32
    // Windows: 使用 signal() 注册控制台事件
    if (std::signal(SIGINT, signal_handler) == SIG_ERR) {
        spdlog::error("注册 SIGINT 处理失败");
    }
    if (std::signal(SIGTERM, signal_handler) == SIG_ERR) {
        spdlog::error("注册 SIGTERM 处理失败");
    }
    // Windows 无 SIGPIPE，无需处理
#else
    // Unix: 使用 sigaction 提供更可靠的处理
    struct sigaction sa {};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, nullptr) != 0) {
        spdlog::error("注册 SIGINT 处理失败");
    }
    if (sigaction(SIGTERM, &sa, nullptr) != 0) {
        spdlog::error("注册 SIGTERM 处理失败");
    }

    // 忽略 SIGPIPE（防止写已关闭的 socket 时进程退出）
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, nullptr) != 0) {
        spdlog::error("注册 SIGPIPE 处理失败");
    }
#endif
}

// ========== 日志初始化 ==========

/// 初始化 spdlog 日志系统
/// @param level 日志级别字符串
/// @param log_file 日志文件路径（空字符串表示仅输出到控制台）
void init_logging(const std::string& level, const std::string& log_file) {
    // 创建控制台 sink
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");

    // 创建文件 sink
    std::shared_ptr<spdlog::sinks::basic_file_sink_mt> file_sink;
    if (!log_file.empty()) {
        file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
    }

    // 创建多 sink logger
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(console_sink);
    if (file_sink) {
        sinks.push_back(file_sink);
    }

    auto logger = std::make_shared<spdlog::logger>("voice-sfu", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::from_str(level));
    spdlog::set_default_logger(logger);

    spdlog::flush_every(std::chrono::seconds(3));

    spdlog::info("日志系统已初始化，级别: {}，文件: {}", level,
                 log_file.empty() ? "(仅控制台)" : log_file);
}

// ========== 命令行参数解析 ==========

/// 解析命令行参数
/// @param argc 参数个数
/// @param argv 参数数组
/// @return 配置文件路径
std::string parse_args(int argc, char* argv[]) {
    std::string config_path = "config.yaml";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "用法: voice-sfu-server [选项]\n"
                      << "\n"
                      << "选项:\n"
                      << "  --config, -c <路径>  指定配置文件路径 (默认: config.yaml)\n"
                      << "  --help, -h           显示帮助信息\n"
                      << "\n"
                      << "示例:\n"
                      << "  voice-sfu-server\n"
                      << "  voice-sfu-server --config /etc/voice-sfu/config.yaml\n";
            std::exit(0);
        } else {
            std::cerr << "未知参数: " << arg << "\n"
                      << "使用 --help 查看帮助信息\n";
            std::exit(1);
        }
    }

    return config_path;
}

// ========== 主函数 ==========

int main(int argc, char* argv[]) {
    try {
        // 1. 创建 Qt 核心应用
        QCoreApplication app(argc, argv);
        g_app = &app;

        app.setApplicationName("voice-sfu-server");
        app.setOrganizationName("voice-intercom");

        // 2. 解析命令行参数
        std::string config_path = parse_args(argc, argv);
        std::cout << "Voice SFU Server - 语音对讲 SFU 服务端 (Qt5 DTLS 模式)\n";

        // 3. 加载配置文件
        spdlog::info("正在加载配置文件: {}", config_path);
        voice::sfu::ServerConfig config;
        try {
            config = voice::sfu::load_from_yaml(config_path);
        } catch (const std::exception& e) {
            std::cerr << "加载配置文件失败: " << e.what() << "\n"
                      << "将使用默认配置\n";
            config = voice::sfu::ServerConfig();  // 使用默认值
        }

        // 4. 初始化日志
        init_logging(config.log_level, config.log_file);

        spdlog::info("========== Voice SFU Server 启动 ==========");
        spdlog::info("配置信息:");
        spdlog::info("  信令端口: {}", config.signaling_port);
        spdlog::info("  媒体端口范围: {}-{}", config.media_port_min, config.media_port_max);
        spdlog::info("  最大参与者数: {}", config.max_participants);

        // 5. 注册信号处理
        setup_signal_handlers();

        // 6. 创建核心组件
        voice::sfu::RoomManager room_manager;
        voice::sfu::SFUManager  sfu_manager;
        voice::sfu::SignalingServer signaling_server;
        g_signaling_server = &signaling_server;

        // 7. 初始化 SFU 管理器
        sfu_manager.init(config.media_port_min, config.media_port_max);

        // 8. 初始化信令服务器
        if (!signaling_server.init(config.signaling_port, room_manager, sfu_manager)) {
            spdlog::error("信令服务器初始化失败");
            return 1;
        }

        // 9. 连接 Qt 信号槽（替代原有的回调函数）
        QObject::connect(&signaling_server, &voice::sfu::SignalingServer::participantJoined,
                         [&sfu_manager](const QString& room_id, const QString& user_id, const QString& role) {
            spdlog::info("[信号] 参与者加入: room_id={}, user_id={}, role={}",
                         room_id.toStdString(), user_id.toStdString(), role.toStdString());
            // 为新参与者创建音频生产者
            sfu_manager.add_producer(room_id.toStdString(), user_id.toStdString(),
                                     voice::sfu::MediaType::Audio);
        });

        QObject::connect(&signaling_server, &voice::sfu::SignalingServer::participantLeft,
                         [&room_manager, &sfu_manager](const QString& room_id, const QString& user_id) {
            spdlog::info("[信号] 参与者离开: room_id={}, user_id={}",
                         room_id.toStdString(), user_id.toStdString());
            // 清理 SFU 资源
            sfu_manager.remove_participant_transports(room_id.toStdString(), user_id.toStdString());

            // 如果房间为空，移除房间
            if (room_manager.participant_count(room_id.toStdString()) == 0) {
                room_manager.remove_room(room_id.toStdString());
                spdlog::info("房间已清空并移除: room_id={}", room_id.toStdString());
            }
        });

        QObject::connect(&signaling_server, &voice::sfu::SignalingServer::audioToggled,
                         [](const QString& room_id, const QString& user_id, bool muted) {
            spdlog::info("[信号] 静音状态变更: room_id={}, user_id={}, muted={}",
                         room_id.toStdString(), user_id.toStdString(), muted);
            // TODO: 通知 SFU 暂停/恢复该参与者的音频流转发
        });

        QObject::connect(&signaling_server, &voice::sfu::SignalingServer::tonePlayed,
                         [](const QString& room_id, const QString& user_id, const QString& tone_type) {
            spdlog::info("[信号] 播放提示音: room_id={}, user_id={}, tone_type={}",
                         room_id.toStdString(), user_id.toStdString(), tone_type.toStdString());
            // TODO: 通过 SFU 向房间内其他参与者播放提示音
        });

        // 10. 启动信令服务器
        if (!signaling_server.start()) {
            spdlog::error("信令服务器启动失败");
            return 1;
        }

        // 11. 进入 Qt 事件循环
        spdlog::info("服务端已就绪，等待 DTLS 连接... (按 Ctrl+C 退出)");
        int ret = app.exec();

        // 12. 优雅关闭
        spdlog::info("正在关闭服务端...");
        signaling_server.stop();
        sfu_manager.shutdown();

        spdlog::info("========== Voice SFU Server 已关闭 ==========");
        return ret;

    } catch (const std::exception& e) {
        spdlog::critical("未捕获的异常: {}", e.what());
        return 1;
    } catch (...) {
        spdlog::critical("未知的异常");
        return 1;
    }
}
