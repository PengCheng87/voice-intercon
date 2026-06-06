#include "signaling_server.h"
#include "room_manager.h"
#include "sfu_manager.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <QSslKey>
#include <QSslCertificate>
#include <QDateTime>
#include <QFile>

namespace voice::sfu {

// ========== 辅助函数 ==========

/// 生成对端唯一标识键
static QString peer_key(const QHostAddress& addr, quint16 port) {
    return addr.toString() + ":" + QString::number(port);
}

// ========== 构造/析构 ==========

SignalingServer::SignalingServer(QObject* parent)
    : QObject(parent)
    , udp_socket_(new QUdpSocket(this))
    , dtls_verifier_(new QDtlsClientVerifier(this))
    , heartbeat_timer_(new QTimer(this))
    , handshake_timer_(new QTimer(this))
{
    // 连接 UDP socket 数据到达信号
    connect(udp_socket_, &QUdpSocket::readyRead,
            this, &SignalingServer::onReadyRead);

    // 连接心跳定时器
    connect(heartbeat_timer_, &QTimer::timeout,
            this, &SignalingServer::onHeartbeatTimeout);

    // 连接握手超时定时器
    connect(handshake_timer_, &QTimer::timeout,
            this, &SignalingServer::onHandshakeTimeout);
}

SignalingServer::~SignalingServer() {
    stop();
}

// ========== 初始化 ==========

bool SignalingServer::init(uint16_t port, RoomManager& room_manager, SFUManager& sfu_manager) {
    port_         = port;
    room_manager_ = &room_manager;
    sfu_manager_  = &sfu_manager;

    // 设置 DTLS 证书
    if (!setup_dtls_certificate()) {
        spdlog::error("DTLS 证书设置失败");
        return false;
    }

    spdlog::info("信令服务器初始化完成，监听端口: {}", port);
    return true;
}

// ========== 启动/停止 ==========

bool SignalingServer::start() {
    if (udp_socket_->state() == QUdpSocket::BoundState) {
        spdlog::warn("信令服务器已在运行中");
        return false;
    }

    // 绑定 UDP 端口
    if (!udp_socket_->bind(QHostAddress::Any, port_)) {
        spdlog::error("绑定 UDP 端口失败: {}", port_);
        return false;
    }

    spdlog::info("信令服务器已启动，UDP 端口: {}", port_);

    // 启动心跳检测定时器
    heartbeat_timer_->start(HEARTBEAT_INTERVAL_MS);

    // 启动握手超时检测定时器
    handshake_timer_->start(HANDSHAKE_TIMEOUT_MS);

    return true;
}

void SignalingServer::stop() {
    spdlog::info("信令服务器正在关闭...");

    // 停止定时器
    heartbeat_timer_->stop();
    handshake_timer_->stop();

    // 断开所有客户端
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : clients_) {
            client->alive = false;
            if (client->dtls) {
                client->dtls->shutdown(udp_socket_);
            }
        }
        clients_.clear();
        peer_to_user_.clear();
    }

    // 关闭 UDP socket
    udp_socket_->close();

    spdlog::info("信令服务器已关闭");
}

// ========== 消息广播 ==========

void SignalingServer::broadcast_to_room(const std::string& room_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    QString q_room_id = QString::fromStdString(room_id);
    QByteArray data = QByteArray::fromStdString(message);

    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        auto& client = it.value();
        if (client->room_id == q_room_id && client->alive && client->dtls) {
            if (client->dtls->handshakeState() == QDtls::HandshakeComplete) {
                send_dtls_data(client, data);
            }
        }
    }
}

bool SignalingServer::send_to_user(const std::string& user_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto client = get_client(QString::fromStdString(user_id));
    if (!client || !client->alive || !client->dtls) {
        spdlog::warn("发送消息失败，用户不在线或未完成握手: {}", user_id);
        return false;
    }

    if (client->dtls->handshakeState() != QDtls::HandshakeComplete) {
        spdlog::warn("发送消息失败，DTLS 握手未完成: {}", user_id);
        return false;
    }

    QByteArray data = QByteArray::fromStdString(message);
    return send_dtls_data(client, data);
}

size_t SignalingServer::connection_count() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return static_cast<size_t>(clients_.size());
}

// ========== DTLS 数据接收 ==========

void SignalingServer::onReadyRead() {
    while (udp_socket_->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(udp_socket_->pendingDatagramSize()));

        QHostAddress peer_address;
        quint16 peer_port = 0;
        qint64 read_len = udp_socket_->readDatagram(datagram.data(), datagram.size(),
                                                     &peer_address, &peer_port);
        if (read_len <= 0) {
            continue;
        }
        datagram.resize(static_cast<int>(read_len));

        // 查找或创建客户端连接
        auto client = find_or_create_client(peer_address, peer_port);
        if (!client) {
            continue;
        }

        // 阶段1：Cookie 验证（防止 DTLS 放大攻击）
        if (!client->verified) {
            bool verified = dtls_verifier_->verifyClient(udp_socket_, datagram,
                                                         peer_address, peer_port);
            if (!verified) {
                // Cookie 验证失败，但可能是正常的 ClientHello 重传
                // QDtlsClientVerifier 会自动发送 HelloVerifyRequest
                continue;
            }
            // Cookie 验证通过
            client->verified = true;
            spdlog::debug("DTLS Cookie 验证通过: {}:{}",
                          peer_address.toString().toStdString(), peer_port);
        }

        // 阶段2：DTLS 握手或数据传输
        if (!client->dtls) {
            // 创建 QDtls 对象并设置对端信息
            client->dtls = new QDtls(QSslSocket::SslServerMode, this);
            client->dtls->setDtlsConfiguration(ssl_config_);
            client->dtls->setPeer(peer_address, peer_port);
            spdlog::debug("新建 DTLS 连接对象: {}:{}",
                          peer_address.toString().toStdString(), peer_port);
        }

        // 处理 DTLS 握手或解密数据
        QDtls::HandshakeState state = client->dtls->handshakeState();

        if (state != QDtls::HandshakeComplete) {
            // 继续握手
            bool result = client->dtls->doHandshake(udp_socket_, datagram);
            if (!result) {
                spdlog::warn("DTLS 握手失败: {}:{}, error={}",
                             peer_address.toString().toStdString(), peer_port,
                             client->dtls->dtlsErrorString().toStdString());
                disconnect_client(client->user_id);
                continue;
            }

            if (client->dtls->handshakeState() == QDtls::HandshakeComplete) {
                spdlog::info("DTLS 握手完成: {}:{}",
                             peer_address.toString().toStdString(), peer_port);
                client->last_active = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
            }
            continue;
        }

        // 握手已完成，解密数据
        QByteArray plain_text = client->dtls->decryptDatagram(udp_socket_, datagram);
        if (plain_text.isEmpty()) {
            // 可能是 DTLS 控制消息（如 alert）
            continue;
        }

        // 更新活跃时间
        client->last_active = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();

        // 处理解密后的消息
        std::string raw_message = plain_text.toStdString();
        handle_message(client, raw_message);
    }
}

// ========== 心跳检测 ==========

void SignalingServer::onHeartbeatTimeout() {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    QList<QString> timed_out_users;

    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        auto& client = it.value();
        if (!client->alive) {
            continue;
        }

        qint64 elapsed = now - client->last_active;
        if (elapsed > HEARTBEAT_TIMEOUT_MS) {
            timed_out_users.append(it.key());
        }
    }

    // 断开超时客户端
    for (const auto& uid : timed_out_users) {
        spdlog::warn("客户端心跳超时，断开连接: user_id={}", uid.toStdString());

        auto client = clients_.value(uid);
        if (client && !client->room_id.isEmpty()) {
            QString room_id = client->room_id;
            std::string std_room = room_id.toStdString();
            std::string std_uid  = uid.toStdString();

            // 从房间移除
            room_manager_->remove_participant(std_room, std_uid);
            sfu_manager_->remove_participant_transports(std_room, std_uid);

            // 广播离开消息
            json broadcast_data;
            broadcast_data["room_id"] = std_room;
            broadcast_data["user_id"] = std_uid;
            std::string broadcast_msg = build_response("participant_left", broadcast_data);

            // 在锁内直接发送给所有其他客户端
            QByteArray data = QByteArray::fromStdString(broadcast_msg);
            for (auto cit = clients_.begin(); cit != clients_.end(); ++cit) {
                auto& c = cit.value();
                if (c->room_id == room_id && c->alive && c->dtls &&
                    c->dtls->handshakeState() == QDtls::HandshakeComplete) {
                    send_dtls_data(c, data);
                }
            }
        }
    }

    // 移除超时客户端
    for (const auto& uid : timed_out_users) {
        auto client = clients_.value(uid);
        if (client) {
            QString peer_key_str = peer_key(client->peer_address, client->peer_port);
            peer_to_user_.remove(peer_key_str);
            clients_.remove(uid);
        }
    }

    // 向所有存活客户端发送心跳 ping
    json ping_msg;
    ping_msg["type"] = "ping";
    ping_msg["data"] = "ping";
    QByteArray ping_data = QByteArray::fromStdString(ping_msg.dump());

    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        auto& client = it.value();
        if (client->alive && client->dtls &&
            client->dtls->handshakeState() == QDtls::HandshakeComplete) {
            send_dtls_data(client, ping_data);
        }
    }
}

// ========== 握手超时处理 ==========

void SignalingServer::onHandshakeTimeout() {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    QList<QString> timeout_clients;

    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        auto& client = it.value();
        if (!client->alive) {
            continue;
        }
        if (client->dtls && client->dtls->handshakeState() != QDtls::HandshakeComplete) {
            qint64 elapsed = now - client->last_active;
            if (elapsed > HANDSHAKE_TIMEOUT_MS) {
                timeout_clients.append(it.key());
            }
        }
    }

    for (const auto& uid : timeout_clients) {
        spdlog::warn("DTLS 握手超时，断开连接: user_id={}", uid.toStdString());
        auto client = clients_.value(uid);
        if (client) {
            QString peer_key_str = peer_key(client->peer_address, client->peer_port);
            peer_to_user_.remove(peer_key_str);
            clients_.remove(uid);
        }
    }
}

// ========== 消息处理 ==========

void SignalingServer::handle_message(std::shared_ptr<ClientConnection> client,
                                      const std::string& raw_message) {
    json root;
    try {
        root = json::parse(raw_message);
    } catch (const json::parse_error& e) {
        spdlog::warn("JSON 解析失败: {} (原始消息: {})", e.what(), raw_message);
        return;
    }

    std::string type = root.value("type", "");
    spdlog::debug("收到消息: type={}, user_id={}", type,
                  client->user_id.isEmpty() ? "(未加入)" : client->user_id.toStdString());

    SignalType signal = parse_signal_type(root);

    switch (signal) {
        case SignalType::Join:
            handle_join(client, root);
            break;
        case SignalType::Leave:
            handle_leave(client, root);
            break;
        case SignalType::Mute:
            handle_mute(client, root);
            break;
        case SignalType::PlayTone:
            handle_play_tone(client, root);
            break;
        case SignalType::Heartbeat:
            handle_heartbeat(client);
            break;
        default:
            spdlog::warn("未知的信令消息类型: {}", type);
            break;
    }
}

SignalType SignalingServer::parse_signal_type(const json& msg) {
    std::string type = msg.value("type", "");

    if (type == "join")      return SignalType::Join;
    if (type == "leave")     return SignalType::Leave;
    if (type == "mute")      return SignalType::Mute;
    if (type == "play_tone") return SignalType::PlayTone;
    if (type == "heartbeat") return SignalType::Heartbeat;

    return SignalType::Unknown;
}

// ========== Join 处理 ==========

void SignalingServer::handle_join(std::shared_ptr<ClientConnection> client, const json& msg) {
    std::string room_id_str = msg.value("room_id", "");
    std::string user_id_str = msg.value("user_id", "");
    std::string role        = msg.value("role", "doctor");

    QString room_id = QString::fromStdString(room_id_str);
    QString user_id = QString::fromStdString(user_id_str);

    if (room_id_str.empty() || user_id_str.empty()) {
        std::string error_resp = build_response("error", "room_id 和 user_id 不能为空");
        send_to_user(user_id_str, error_resp);
        return;
    }

    // 检查用户是否已经加入
    if (!client->user_id.isEmpty() && !client->user_id.startsWith("pending_")) {
        std::string error_resp = build_response("error", "已经加入房间，请先离开");
        send_to_user(user_id_str, error_resp);
        return;
    }

    // 解析角色
    ParticipantRole participant_role = ParticipantRole::Doctor;
    if (role == "host") {
        participant_role = ParticipantRole::Host;
    }

    // 创建房间（如果不存在）
    room_manager_->create_room(room_id_str);

    // 创建参与者
    Participant participant;
    participant.user_id       = user_id_str;
    participant.role          = participant_role;
    participant.audio_muted   = false;
    participant.audio_enabled = true;
    participant.connected_at  = std::chrono::system_clock::now();
    participant.ws_handle     = nullptr; // DTLS 模式下不使用原生句柄

    // 添加到房间
    if (!room_manager_->add_participant(room_id_str, participant)) {
        std::string error_resp = build_response("error", "加入房间失败，房间已满或不存在");
        send_to_user(user_id_str, error_resp);
        return;
    }

    // 创建 SFU 传输
    auto transport = sfu_manager_->create_transport(room_id_str, user_id_str);

    // 更新客户端连接信息
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        // 更新 user_id 映射
        QString old_peer_key = peer_key(client->peer_address, client->peer_port);
        peer_to_user_[old_peer_key] = user_id;

        // 如果之前有临时 key，迁移数据
        if (client->user_id.startsWith("pending_")) {
            clients_.remove(client->user_id);
        }

        client->user_id = user_id;
        client->room_id = room_id;
        clients_[user_id] = client;
    }

    // 触发信号
    emit participantJoined(room_id, user_id, QString::fromStdString(role));

    // 构建加入成功响应
    json data;
    data["room_id"]      = room_id_str;
    data["user_id"]      = user_id_str;
    data["participants"] = room_manager_->get_participants_json(room_id_str);
    data["transport"]    = json::object();
    data["transport"]["transport_id"] = transport.transport_id;
    data["transport"]["port"]         = static_cast<int>(transport.port);
    data["transport"]["ice_ufrag"]    = transport.ice_ufrag;
    data["transport"]["ice_password"] = transport.ice_password;

    std::string response = build_response("join_accept", data);
    send_to_user(user_id_str, response);

    // 广播新参与者加入
    json broadcast_data;
    broadcast_data["room_id"] = room_id_str;
    broadcast_data["user_id"] = user_id_str;
    broadcast_data["role"]    = role;
    std::string broadcast_msg = build_response("participant_joined", broadcast_data);
    broadcast_to_room(room_id_str, broadcast_msg);

    spdlog::info("用户已加入房间: user_id={}, room_id={}, role={}",
                 user_id_str, room_id_str, role);
}

// ========== Leave 处理 ==========

void SignalingServer::handle_leave(std::shared_ptr<ClientConnection> client, const json& msg) {
    QString user_id = client->user_id;
    QString room_id = client->room_id;

    if (user_id.isEmpty() || room_id.isEmpty()) {
        spdlog::warn("无效的 leave 消息: user_id 或 room_id 为空");
        return;
    }

    std::string std_uid  = user_id.toStdString();
    std::string std_room = room_id.toStdString();

    // 从房间移除参与者
    room_manager_->remove_participant(std_room, std_uid);

    // 清理 SFU 传输
    sfu_manager_->remove_participant_transports(std_room, std_uid);

    // 触发信号
    emit participantLeft(room_id, user_id);

    // 广播参与者离开
    json broadcast_data;
    broadcast_data["room_id"] = std_room;
    broadcast_data["user_id"] = std_uid;
    std::string broadcast_msg = build_response("participant_left", broadcast_data);
    broadcast_to_room(std_room, broadcast_msg);

    // 断开客户端
    disconnect_client(user_id);

    spdlog::info("用户已离开房间: user_id={}, room_id={}", std_uid, std_room);
}

// ========== Mute 处理 ==========

void SignalingServer::handle_mute(std::shared_ptr<ClientConnection> client, const json& msg) {
    QString user_id = client->user_id;
    QString room_id = client->room_id;
    bool muted = msg.value("muted", false);

    if (user_id.isEmpty() || room_id.isEmpty()) {
        spdlog::warn("无效的 mute 消息: user_id 或 room_id 为空");
        return;
    }

    std::string std_uid  = user_id.toStdString();
    std::string std_room = room_id.toStdString();

    // 更新参与者静音状态
    auto* participant = room_manager_->get_participant(std_room, std_uid);
    if (participant) {
        participant->audio_muted = muted;
    }

    // 触发信号
    emit audioToggled(room_id, user_id, muted);

    // 广播静音状态变更
    json broadcast_data;
    broadcast_data["room_id"] = std_room;
    broadcast_data["user_id"] = std_uid;
    broadcast_data["muted"]   = muted;
    std::string broadcast_msg = build_response("audio_toggle", broadcast_data);
    broadcast_to_room(std_room, broadcast_msg);

    spdlog::info("用户静音状态变更: user_id={}, room_id={}, muted={}",
                 std_uid, std_room, muted);
}

// ========== PlayTone 处理 ==========

void SignalingServer::handle_play_tone(std::shared_ptr<ClientConnection> client, const json& msg) {
    QString user_id = client->user_id;
    QString room_id = client->room_id;
    std::string tone_type = msg.value("tone_type", "");

    if (user_id.isEmpty() || room_id.isEmpty() || tone_type.empty()) {
        spdlog::warn("无效的 play_tone 消息: 缺少必要字段");
        return;
    }

    std::string std_uid  = user_id.toStdString();
    std::string std_room = room_id.toStdString();

    // 触发信号
    emit tonePlayed(room_id, user_id, QString::fromStdString(tone_type));

    // 广播提示音播放
    json broadcast_data;
    broadcast_data["room_id"]   = std_room;
    broadcast_data["user_id"]   = std_uid;
    broadcast_data["tone_type"] = tone_type;
    std::string broadcast_msg = build_response("tone_played", broadcast_data);
    broadcast_to_room(std_room, broadcast_msg);

    spdlog::info("提示音播放: user_id={}, room_id={}, tone_type={}",
                 std_uid, std_room, tone_type);
}

// ========== Heartbeat 处理 ==========

void SignalingServer::handle_heartbeat(std::shared_ptr<ClientConnection> client) {
    client->last_active = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();

    // 回复心跳确认
    std::string response = build_response("heartbeat_ack", "pong");
    std::string user_id = client->user_id.isEmpty()
        ? peer_key(client->peer_address, client->peer_port).toStdString()
        : client->user_id.toStdString();
    send_to_user(user_id, response);
}

// ========== 断开连接 ==========

void SignalingServer::disconnect_client(const QString& user_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto client = clients_.value(user_id);
    if (client) {
        client->alive = false;
        if (client->dtls) {
            client->dtls->shutdown(udp_socket_);
        }

        QString peer_key_str = peer_key(client->peer_address, client->peer_port);
        peer_to_user_.remove(peer_key_str);
        clients_.remove(user_id);

        spdlog::info("已断开客户端连接: user_id={}", user_id.toStdString());
    }
}

// ========== 客户端查找 ==========

std::shared_ptr<ClientConnection> SignalingServer::find_or_create_client(
    const QHostAddress& address, quint16 port) {

    std::lock_guard<std::mutex> lock(clients_mutex_);

    QString key = peer_key(address, port);
    QString user_id = peer_to_user_.value(key);

    if (!user_id.isEmpty()) {
        auto client = clients_.value(user_id);
        if (client) {
            return client;
        }
    }

    // 创建新连接（临时以 peer_key 作为标识）
    auto client = std::make_shared<ClientConnection>();
    client->peer_address = address;
    client->peer_port    = port;
    client->last_active  = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    client->alive        = true;
    client->verified     = false;

    QString temp_id = "pending_" + key;
    client->user_id = temp_id;
    clients_[temp_id] = client;
    peer_to_user_[key] = temp_id;

    return client;
}

std::shared_ptr<ClientConnection> SignalingServer::get_client(const QString& user_id) {
    return clients_.value(user_id);
}

// ========== DTLS 数据发送 ==========

bool SignalingServer::send_dtls_data(std::shared_ptr<ClientConnection> client,
                                      const QByteArray& data) {
    if (!client || !client->dtls || !client->alive) {
        return false;
    }

    qint64 written = client->dtls->writeDatagramEncrypted(udp_socket_, data);
    if (written < 0) {
        spdlog::warn("DTLS 加密发送失败: user_id={}, error={}",
                     client->user_id.toStdString(),
                     client->dtls->dtlsErrorString().toStdString());
        return false;
    }

    return true;
}

// ========== DTLS 证书设置 ==========

bool SignalingServer::setup_dtls_certificate() {
    // 尝试从文件加载证书
    const QString cert_file = "server.crt";
    const QString key_file  = "server.key";

    if (QFile::exists(cert_file) && QFile::exists(key_file)) {
        QList<QSslCertificate> certs = QSslCertificate::fromPath(cert_file);
        if (!certs.isEmpty()) {
            QSslKey key;
            QFile key_f(key_file);
            if (key_f.open(QIODevice::ReadOnly)) {
                key = QSslKey(key_f.readAll(), QSsl::Rsa);
                key_f.close();
            }

            if (!key.isNull()) {
                ssl_config_.setLocalCertificate(certs.first());
                ssl_config_.setPrivateKey(key);
                ssl_config_.setProtocol(QSsl::DtlsV1_2);
                ssl_config_.setPeerVerifyMode(QSslSocket::VerifyNone);
                spdlog::info("已从文件加载 DTLS 证书");
                return true;
            }
        }
    }

    // 生成自签名证书
    spdlog::info("未找到证书文件，生成自签名 DTLS 证书...");

    // 使用 openssl 命令行生成临时证书
    int ret = std::system(
        "openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt "
        "-days 365 -nodes -subj '/CN=voice-sfu-server' 2>/dev/null"
    );

    if (ret == 0 && QFile::exists("server.crt") && QFile::exists("server.key")) {
        QList<QSslCertificate> certs = QSslCertificate::fromPath("server.crt");
        if (!certs.isEmpty()) {
            QFile key_f("server.key");
            QSslKey key;
            if (key_f.open(QIODevice::ReadOnly)) {
                key = QSslKey(key_f.readAll(), QSsl::Rsa);
                key_f.close();
            }

            if (!key.isNull()) {
                ssl_config_.setLocalCertificate(certs.first());
                ssl_config_.setPrivateKey(key);
                ssl_config_.setProtocol(QSsl::DtlsV1_2);
                ssl_config_.setPeerVerifyMode(QSslSocket::VerifyNone);
                spdlog::info("自签名 DTLS 证书生成成功");
                return true;
            }
        }
    }

    spdlog::error("DTLS 证书设置失败，无法加载或生成证书");
    return false;
}

// ========== 工具函数 ==========

std::string SignalingServer::build_response(const std::string& type, const json& data) {
    json root;
    root["type"] = type;
    root["data"] = data;

    return root.dump();
}

} // namespace voice::sfu
