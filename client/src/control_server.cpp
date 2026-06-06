#include "control_server.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

namespace voice::client {

// ============================================================
// 构造/析构
// ============================================================
ControlServer::ControlServer() {
    spdlog::debug("控制服务器已创建");
}

ControlServer::~ControlServer() {
    stop();
    spdlog::debug("控制服务器已销毁");
}

// ============================================================
// 设置命令处理器
// ============================================================
void ControlServer::set_command_handler(ControlCommandHandler handler) {
    command_handler_ = std::move(handler);
}

// ============================================================
// 启动控制服务器
// ============================================================
bool ControlServer::start(int port) {
    if (running_) {
        spdlog::warn("控制服务器已在运行，端口: {}", port_);
        return true;
    }

    port_ = port;
    running_ = true;

    // 启动接受连接线程
    accept_thread_ = std::thread(&ControlServer::accept_loop, this);

    return true;
}

// ============================================================
// 停止控制服务器
// ============================================================
void ControlServer::stop() {
    if (!running_) return;

    spdlog::info("正在停止控制服务器...");

    running_ = false;

    if (tcp_server_) {
        tcp_server_->close();
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    spdlog::info("控制服务器已停止");
}

// ============================================================
// 接受连接循环（使用QTcpServer实现，跨平台）
// ============================================================
void ControlServer::accept_loop() {
    spdlog::debug("控制服务器接受线程已启动");

    // 在子线程中创建QTcpServer（QTcpServer可在非主线程使用）
    tcp_server_ = new QTcpServer();

    if (!tcp_server_->listen(QHostAddress::LocalHost, port_)) {
        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            last_error_ = "绑定端口失败: " + tcp_server_->errorString().toStdString();
        }
        spdlog::error("控制服务器监听端口 {} 失败: {}", port_, last_error_);
        delete tcp_server_;
        tcp_server_ = nullptr;
        running_ = false;
        return;
    }

    spdlog::info("控制服务器已启动，监听 127.0.0.1:{}", port_);

    while (running_) {
        // waitForNewConnection 超时 1 秒，支持非阻塞轮询 running_ 标志
        if (tcp_server_->waitForNewConnection(1000)) {
            while (tcp_server_->hasPendingConnections()) {
                QTcpSocket* socket = tcp_server_->nextPendingConnection();
                QString peer_info = socket->peerAddress().toString() + ":" +
                                    QString::number(socket->peerPort());
                spdlog::debug("控制连接来自: {}", peer_info.toStdString());

                // 读取命令（单行文本，超时5秒）
                if (socket->waitForReadyRead(5000)) {
                    QByteArray data = socket->readAll();
                    std::string line = QString(data).trimmed().toStdString();

                    spdlog::debug("收到控制命令: {}", line);

                    // 解析命令格式: COMMAND [JSON_PARAMS]
                    std::string command;
                    std::string params = "{}";

                    std::size_t space_pos = line.find(' ');
                    if (space_pos != std::string::npos) {
                        command = line.substr(0, space_pos);
                        params  = line.substr(space_pos + 1);
                    } else {
                        command = line;
                    }

                    // 执行命令
                    std::string response;
                    if (command_handler_) {
                        try {
                            response = command_handler_(command, params);
                        } catch (const std::exception& e) {
                            spdlog::error("处理控制命令异常: {}", e.what());
                            nlohmann::json err_resp;
                            err_resp["success"] = false;
                            err_resp["error"]   = std::string("处理命令异常: ") + e.what();
                            response = err_resp.dump();
                        }
                    } else {
                        nlohmann::json err_resp;
                        err_resp["success"] = false;
                        err_resp["error"]   = "未设置命令处理器";
                        response = err_resp.dump();
                    }

                    // 发送响应
                    socket->write(response.c_str(), static_cast<qint64>(response.size()));
                    socket->write("\n", 1);
                    socket->waitForBytesWritten(3000);
                }

                socket->disconnectFromHost();
                socket->deleteLater();
            }
        }
    }

    // 清理
    if (tcp_server_) {
        tcp_server_->close();
        delete tcp_server_;
        tcp_server_ = nullptr;
    }

    spdlog::debug("控制服务器接受线程已退出");
}

// ============================================================
// 发送命令（静态方法，使用QTcpSocket实现，跨平台）
// ============================================================
std::string ControlServer::send_command(int port, const std::string& command, const std::string& params) {
    QTcpSocket socket;

    // 连接到127.0.0.1:port
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(3000)) {
        return "{\"success\":false,\"error\":\"无法连接到voice-client进程，请确认程序正在运行\"}";
    }

    // 发送命令
    std::string request = command + " " + params + "\n";
    socket.write(request.c_str(), static_cast<qint64>(request.size()));
    socket.waitForBytesWritten(3000);

    // 读取响应
    if (!socket.waitForReadyRead(5000)) {
        socket.disconnectFromHost();
        return "{\"success\":false,\"error\":\"读取响应失败或超时\"}";
    }

    QByteArray response_data = socket.readAll();
    socket.disconnectFromHost();

    std::string response = QString(response_data).trimmed().toStdString();
    return response;
}

// ============================================================
// 状态查询
// ============================================================
bool ControlServer::is_running() const {
    return running_;
}

int ControlServer::get_port() const {
    return port_;
}

std::string ControlServer::get_last_error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

} // namespace voice::client
