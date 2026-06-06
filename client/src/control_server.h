#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>

class QTcpServer;

namespace voice::client {

/// 控制命令处理器回调
/// @param command 命令名称
/// @param params  命令参数（JSON字符串）
/// @return 响应JSON字符串
using ControlCommandHandler = std::function<std::string(const std::string& command, const std::string& params)>;

/// 本地控制服务器 - 监听TCP端口接收控制命令
/// 基于Qt QTcpServer实现，跨平台
class ControlServer {
public:
    ControlServer();
    ~ControlServer();

    // 禁止拷贝
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    /// 设置命令处理器
    void set_command_handler(ControlCommandHandler handler);

    /// 启动控制服务器
    /// @param port 监听端口
    /// @return true表示启动成功
    bool start(int port);

    /// 停止控制服务器
    void stop();

    /// 是否正在运行
    bool is_running() const;

    /// 获取监听端口
    int get_port() const;

    /// 获取最后错误信息
    std::string get_last_error() const;

    /// 向远程控制服务器发送命令（用于CLI子命令）
    /// @param port 目标端口
    /// @param command 命令名称
    /// @param params  命令参数（JSON字符串）
    /// @return 响应JSON字符串，失败时返回空
    static std::string send_command(int port, const std::string& command, const std::string& params = "{}");

private:
    /// 接受连接线程
    void accept_loop();

    /// 处理单个客户端连接
    /// @param socket_fd 原生socket描述符（跨平台POSIX兼容）
    void handle_client(int socket_fd);

    /// 读取一行数据（直到\n）
    static bool read_line(int fd, std::string& line, std::size_t max_len = 4096);

    /// 写入数据
    static bool write_data(int fd, const std::string& data);

    // QTcpServer（在accept线程中创建）
    QTcpServer* tcp_server_ = nullptr;

    // 监听端口
    int port_ = 0;

    // 运行状态
    std::atomic<bool> running_{false};

    // 接受连接线程
    std::thread accept_thread_;

    // 命令处理器
    ControlCommandHandler command_handler_;

    // 错误信息
    mutable std::mutex error_mutex_;
    std::string last_error_;
};

} // namespace voice::client
