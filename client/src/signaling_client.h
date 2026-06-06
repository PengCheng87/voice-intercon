#pragma once

#include <QObject>
#include <QHostAddress>
#include <QHostInfo>
#include <QTimer>
#include <QUdpSocket>
#include <QDtls>
#include <nlohmann/json.hpp>
#include <memory>
#include <vector>
#include <mutex>
#include <string>

namespace voice::client {

/// 参与者信息
struct Participant {
    std::string user_id;  ///< 用户ID
    std::string role;     ///< 角色
    bool        muted     = false;  ///< 是否静音
};

/// 信令客户端 - 基于Qt5 DTLS实现
class SignalingClient : public QObject {
    Q_OBJECT

public:
    explicit SignalingClient(QObject* parent = nullptr);
    ~SignalingClient() override;

    // 禁止拷贝
    SignalingClient(const SignalingClient&) = delete;
    SignalingClient& operator=(const SignalingClient&) = delete;

    /// 连接DTLS信令服务器
    /// @param host 服务器主机名或IP
    /// @param port 服务器端口
    /// @param verify_peer 是否验证服务器证书（开发环境可设为false）
    /// @return true表示开始连接（异步完成）
    bool connect_to_server(const QString& host, quint16 port, bool verify_peer = false);

    /// 断开连接
    void disconnect_from_server();

    /// 发送加入房间请求
    bool send_join(const std::string& room_id, const std::string& user_id, const std::string& role);

    /// 发送离开房间请求
    bool send_leave();

    /// 发送静音/取消静音
    bool send_mute(bool muted);

    /// 是否已连接且握手完成
    bool is_connected() const;

    /// 获取当前参与者列表
    std::vector<Participant> get_participants() const;

    /// 获取最后错误信息
    std::string get_last_error() const;

signals:
    /// 连接成功（DTLS握手完成）
    void connected();

    /// 连接断开
    void disconnected(const QString& reason);

    /// 加入房间被接受
    void joinAccepted(const std::vector<Participant>& participants);

    /// 参与者加入/离开
    void participantUpdate(const Participant& participant, bool joined);

    /// 某人静音状态变化
    void audioToggle(const QString& user_id, bool muted);

    /// 提示音事件
    void tonePlayed(const QString& tone_id, const QString& user_id);

private slots:
    /// UDP数据到达
    void onReadyRead();

    /// DTLS握手完成
    void onHandshakeComplete();

    /// DTLS错误处理
    void onDtlsError(const QString& errorString);

    /// 心跳定时器
    void onHeartbeat();

private:
    /// 发送JSON消息（经过DTLS加密）
    bool send_json_message(const nlohmann::json& msg);

    /// 处理解密后的消息
    void handle_message(const std::string& message);

    /// 设置最后错误信息
    void set_last_error(const std::string& error);

    /// 清理资源
    void cleanup();

    // Qt网络对象
    QUdpSocket* udp_socket_ = nullptr;
    QDtls*      dtls_       = nullptr;

    // 服务器地址
    QHostAddress server_address_;
    quint16      server_port_ = 0;

    // 心跳定时器
    QTimer* heartbeat_timer_ = nullptr;

    // 参与者列表（线程安全）
    mutable std::mutex participants_mutex_;
    std::vector<Participant> participants_;

    // 错误信息
    mutable std::mutex error_mutex_;
    std::string last_error_;

    // 连接状态
    bool handshake_complete_ = false;
};

} // namespace voice::client
