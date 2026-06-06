#include "signaling_client.h"

#include <QSslConfiguration>
#include <QNetworkDatagram>

#include <spdlog/spdlog.h>

namespace voice::client {

// ============================================================
// 构造/析构
// ============================================================
SignalingClient::SignalingClient(QObject* parent)
    : QObject(parent)
{
    spdlog::debug("信令客户端已创建");
}

SignalingClient::~SignalingClient() {
    cleanup();
    spdlog::debug("信令客户端已销毁");
}

// ============================================================
// 连接服务器（DTLS over UDP）
// ============================================================
bool SignalingClient::connect_to_server(const QString& host, quint16 port, bool verify_peer) {
    if (udp_socket_) {
        spdlog::warn("已经连接到服务器");
        return true;
    }

    // 创建UDP Socket
    udp_socket_ = new QUdpSocket(this);

    // 绑定本地端口（任意可用端口）
    if (!udp_socket_->bind()) {
        set_last_error("绑定本地UDP端口失败: " + udp_socket_->errorString().toStdString());
        spdlog::error("绑定本地UDP端口失败: {}", udp_socket_->errorString().toStdString());
        cleanup();
        return false;
    }

    spdlog::debug("本地UDP端口已绑定: {}", udp_socket_->localPort());

    // 解析服务器地址
    server_address_ = QHostAddress(host);
    if (server_address_.isNull()) {
        // 尝试DNS解析
        QHostInfo info = QHostInfo::fromName(host);
        if (!info.addresses().isEmpty()) {
            server_address_ = info.addresses().first();
        } else {
            set_last_error("无法解析服务器地址: " + host.toStdString());
            spdlog::error("无法解析服务器地址: {}", host.toStdString());
            cleanup();
            return false;
        }
    }
    server_port_ = port;

    // 创建QDtls客户端
    dtls_ = new QDtls(QSslSocket::SslClientMode, this);

    // 设置对端地址
    if (!dtls_->setPeer(server_address_, server_port_)) {
        set_last_error("设置DTLS对端失败: " + dtls_->dtlsErrorString().toStdString());
        spdlog::error("设置DTLS对端失败: {}", dtls_->dtlsErrorString().toStdString());
        cleanup();
        return false;
    }

    // 配置SSL参数
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    if (!verify_peer) {
        // 开发环境：跳过证书验证
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        spdlog::warn("DTLS证书验证已关闭（开发模式）");
    } else {
        config.setPeerVerifyMode(QSslSocket::VerifyPeer);
    }
    dtls_->setDtlsConfiguration(config);

    // 连接信号
    connect(udp_socket_, &QUdpSocket::readyRead, this, &SignalingClient::onReadyRead);
    connect(dtls_, &QDtls::handshakeTimeout, this, [this]() {
        spdlog::warn("DTLS握手超时，正在重试...");
        if (!dtls_->handleTimeout(udp_socket_)) {
            set_last_error("DTLS握手重试失败");
            emit disconnected("DTLS握手超时");
            cleanup();
        }
    });

    // 开始DTLS握手
    spdlog::info("正在连接DTLS服务器 {}:{} ...", host.toStdString(), port);
    bool ok = dtls_->doHandshake(udp_socket_);
    if (!ok) {
        set_last_error("DTLS握手初始化失败: " + dtls_->dtlsErrorString().toStdString());
        spdlog::error("DTLS握手初始化失败: {}", dtls_->dtlsErrorString().toStdString());
        cleanup();
        return false;
    }

    // 创建心跳定时器（每30秒）
    heartbeat_timer_ = new QTimer(this);
    heartbeat_timer_->setInterval(30000); // 30秒
    connect(heartbeat_timer_, &QTimer::timeout, this, &SignalingClient::onHeartbeat);

    return true;
}

// ============================================================
// 断开连接
// ============================================================
void SignalingClient::disconnect_from_server() {
    if (!udp_socket_) {
        return;
    }

    spdlog::info("正在断开信令连接...");

    // 发送离开消息（如果已连接）
    if (handshake_complete_) {
        send_leave();
    }

    // 关闭DTLS会话
    if (dtls_ && dtls_->isConnectionEncrypted()) {
        dtls_->shutdown(udp_socket_);
    }

    cleanup();
    spdlog::info("信令连接已断开");
}

// ============================================================
// 清理资源
// ============================================================
void SignalingClient::cleanup() {
    // 停止心跳
    if (heartbeat_timer_) {
        heartbeat_timer_->stop();
        delete heartbeat_timer_;
        heartbeat_timer_ = nullptr;
    }

    // 关闭UDP Socket
    if (udp_socket_) {
        udp_socket_->close();
        delete udp_socket_;
        udp_socket_ = nullptr;
    }

    // 释放DTLS
    if (dtls_) {
        delete dtls_;
        dtls_ = nullptr;
    }

    handshake_complete_ = false;
    server_port_ = 0;

    {
        std::lock_guard<std::mutex> lock(participants_mutex_);
        participants_.clear();
    }
}

// ============================================================
// UDP数据到达处理
// ============================================================
void SignalingClient::onReadyRead() {
    if (!udp_socket_ || !dtls_) {
        return;
    }

    while (udp_socket_->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udp_socket_->receiveDatagram();

        // 检查是否来自服务器
        if (datagram.senderAddress() != server_address_ || datagram.senderPort() != server_port_) {
            spdlog::warn("收到来自未知来源的数据包: {}:{}",
                         datagram.senderAddress().toString().toStdString(),
                         datagram.senderPort());
            continue;
        }

        QByteArray data = datagram.data();

        // DTLS握手阶段
        if (!handshake_complete_) {
            bool ok = dtls_->doHandshake(udp_socket_, data);
            if (!ok) {
                set_last_error("DTLS握手失败: " + dtls_->dtlsErrorString().toStdString());
                spdlog::error("DTLS握手失败: {}", dtls_->dtlsErrorString().toStdString());
                emit disconnected("DTLS握手失败");
                cleanup();
                return;
            }

            if (dtls_->handshakeState() == QDtls::HandshakeComplete) {
                onHandshakeComplete();
            }
            continue;
        }

        // 解密数据
        QByteArray decrypted = dtls_->decryptDatagram(udp_socket_, data);
        if (decrypted.isEmpty()) {
            if (dtls_->dtlsError() == QDtlsError::RemoteClosedConnectionError) {
                spdlog::info("服务器关闭DTLS连接");
                emit disconnected("服务器关闭连接");
                cleanup();
                return;
            }
            spdlog::warn("解密数据失败: {}", dtls_->dtlsErrorString().toStdString());
            continue;
        }

        // 处理解密后的消息
        handle_message(decrypted.toStdString());
    }
}

// ============================================================
// DTLS握手完成
// ============================================================
void SignalingClient::onHandshakeComplete() {
    handshake_complete_ = true;
    spdlog::info("DTLS握手完成，加密通道已建立");

    // 启动心跳定时器
    if (heartbeat_timer_) {
        heartbeat_timer_->start();
    }

    emit connected();
}

// ============================================================
// DTLS错误处理
// ============================================================
void SignalingClient::onDtlsError(const QString& errorString) {
    spdlog::error("DTLS错误: {}", errorString.toStdString());
    set_last_error(errorString.toStdString());
    emit disconnected(errorString);
    cleanup();
}

// ============================================================
// 心跳定时器
// ============================================================
void SignalingClient::onHeartbeat() {
    if (!handshake_complete_) {
        return;
    }

    nlohmann::json msg;
    msg["type"]      = "ping";
    msg["timestamp"] = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    send_json_message(msg);
    spdlog::trace("发送心跳");
}

// ============================================================
// 发送JSON消息（经过DTLS加密）
// ============================================================
bool SignalingClient::send_json_message(const nlohmann::json& msg) {
    if (!udp_socket_ || !dtls_ || !handshake_complete_) {
        spdlog::warn("尝试发送数据但未连接");
        return false;
    }

    std::string json_str = msg.dump();
    QByteArray data(json_str.c_str(), static_cast<int>(json_str.size()));

    qint64 written = dtls_->writeDatagramEncrypted(udp_socket_, data);
    if (written < 0) {
        spdlog::error("发送DTLS加密数据失败: {}", dtls_->dtlsErrorString().toStdString());
        return false;
    }

    return true;
}

// ============================================================
// 处理接收到的消息
// ============================================================
void SignalingClient::handle_message(const std::string& message) {
    spdlog::debug("收到消息: {}", message);

    using json = nlohmann::json;
    json root;
    try {
        root = json::parse(message);
    } catch (const json::parse_error& e) {
        spdlog::warn("JSON解析失败: {}", e.what());
        return;
    }

    if (!root.contains("type")) {
        spdlog::warn("消息缺少type字段");
        return;
    }

    std::string type = root["type"].get<std::string>();

    if (type == "join_accept") {
        // 加入房间成功
        std::lock_guard<std::mutex> lock(participants_mutex_);
        participants_.clear();
        const auto& parts = root["participants"];
        if (parts.is_array()) {
            for (const auto& p : parts) {
                Participant part;
                part.user_id = p["user_id"].get<std::string>();
                part.role    = p.value("role", std::string(""));
                part.muted   = p.value("muted", false);
                participants_.push_back(part);
            }
        }
        spdlog::info("加入房间成功，参与者数量: {}", participants_.size());

        emit joinAccepted(participants_);

    } else if (type == "participant_joined") {
        Participant part;
        part.user_id = root["user_id"].get<std::string>();
        part.role    = root.value("role", std::string(""));
        part.muted   = root.value("muted", false);

        {
            std::lock_guard<std::mutex> lock(participants_mutex_);
            participants_.push_back(part);
        }
        spdlog::info("参与者加入: {} ({})", part.user_id, part.role);

        emit participantUpdate(part, true);

    } else if (type == "participant_left") {
        std::string user_id = root["user_id"].get<std::string>();
        Participant left_part;
        left_part.user_id = user_id;

        {
            std::lock_guard<std::mutex> lock(participants_mutex_);
            participants_.erase(
                std::remove_if(participants_.begin(), participants_.end(),
                    [&user_id](const Participant& p) { return p.user_id == user_id; }),
                participants_.end()
            );
        }
        spdlog::info("参与者离开: {}", user_id);

        emit participantUpdate(left_part, false);

    } else if (type == "audio_toggle") {
        std::string user_id = root["user_id"].get<std::string>();
        bool muted = root["muted"].get<bool>();

        {
            std::lock_guard<std::mutex> lock(participants_mutex_);
            for (auto& p : participants_) {
                if (p.user_id == user_id) {
                    p.muted = muted;
                    break;
                }
            }
        }
        spdlog::info("音频状态变化: {} 静音={}", user_id, muted ? "是" : "否");

        emit audioToggle(QString::fromStdString(user_id), muted);

    } else if (type == "tone_played") {
        std::string tone_id = root["tone_id"].get<std::string>();
        std::string user_id = root.value("user_id", std::string(""));
        spdlog::info("提示音: {} 来自 {}", tone_id, user_id);

        emit tonePlayed(QString::fromStdString(tone_id), QString::fromStdString(user_id));

    } else if (type == "pong") {
        // 心跳响应
        spdlog::trace("收到pong");

    } else if (type == "error") {
        std::string error_msg = root["message"].get<std::string>();
        spdlog::error("服务器错误: {}", error_msg);

    } else {
        spdlog::warn("未知消息类型: {}", type);
    }
}

// ============================================================
// 发送加入房间
// ============================================================
bool SignalingClient::send_join(const std::string& room_id, const std::string& user_id, const std::string& role) {
    nlohmann::json msg;
    msg["type"]    = "join";
    msg["room_id"] = room_id;
    msg["user_id"] = user_id;
    msg["role"]    = role;

    spdlog::info("发送加入房间: room={}, user={}, role={}", room_id, user_id, role);
    return send_json_message(msg);
}

// ============================================================
// 发送离开房间
// ============================================================
bool SignalingClient::send_leave() {
    nlohmann::json msg;
    msg["type"] = "leave";

    spdlog::info("发送离开房间");
    return send_json_message(msg);
}

// ============================================================
// 发送静音/取消静音
// ============================================================
bool SignalingClient::send_mute(bool muted) {
    nlohmann::json msg;
    msg["type"]  = "audio_toggle";
    msg["muted"] = muted;

    spdlog::info("发送静音状态: muted={}", muted ? "是" : "否");
    return send_json_message(msg);
}

// ============================================================
// 查询状态
// ============================================================
bool SignalingClient::is_connected() const {
    return handshake_complete_;
}

std::vector<Participant> SignalingClient::get_participants() const {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    return participants_;
}

std::string SignalingClient::get_last_error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void SignalingClient::set_last_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
}

} // namespace voice::client
