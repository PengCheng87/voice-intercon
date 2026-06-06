#pragma once

#include <QObject>
#include <QHostAddress>
#include <QUdpSocket>
#include <QDtls>
#include <QDtlsClientVerifier>
#include <QTimer>
#include <QMap>
#include <QSslConfiguration>
#include <QDateTime>
#include <QFile>

#include <string>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

namespace voice::sfu {

using json = nlohmann::json;

class RoomManager;
class SFUManager;

/// 信令消息类型
enum class SignalType {
    Join,        ///< 加入房间
    Leave,       ///< 离开房间
    Mute,        ///< 静音/取消静音
    PlayTone,    ///< 播放提示音
    Heartbeat,   ///< 心跳
    Unknown      ///< 未知消息
};

/// 客户端连接信息（Qt DTLS 版本）
/// 每个连接对应一个 QDtls 对象，用于加密通信
struct ClientConnection : public std::enable_shared_from_this<ClientConnection> {
    QString       user_id;       ///< 用户ID
    QString       room_id;       ///< 所在房间ID
    QHostAddress  peer_address;  ///< 对端地址
    quint16       peer_port = 0; ///< 对端端口
    QDtls*        dtls = nullptr; ///< QDtls 对象（用于加密通信）
    qint64        last_active = 0; ///< 最后活跃时间（毫秒时间戳）
    bool          alive = true;    ///< 连接是否存活
    bool          verified = false; ///< Cookie 验证是否通过

    // 禁止拷贝和移动
    ClientConnection() = default;
    ClientConnection(const ClientConnection&) = delete;
    ClientConnection& operator=(const ClientConnection&) = delete;
    ClientConnection(ClientConnection&&) = delete;
    ClientConnection& operator=(ClientConnection&&) = delete;

    ~ClientConnection() {
        if (dtls) {
            dtls->shutdown(nullptr);
            delete dtls;
            dtls = nullptr;
        }
    }
};

/// 信令服务器
/// 基于 Qt5 DTLS (QUdpSocket + QDtls + QDtlsClientVerifier) 实现
/// 负责客户端连接管理、DTLS 握手、信令消息处理、心跳检测
class SignalingServer : public QObject {
    Q_OBJECT

public:
    explicit SignalingServer(QObject* parent = nullptr);
    ~SignalingServer() override;

    // 禁止拷贝
    SignalingServer(const SignalingServer&) = delete;
    SignalingServer& operator=(const SignalingServer&) = delete;

    /// 初始化信令服务器
    /// @param port 监听端口
    /// @param room_manager 房间管理器引用
    /// @param sfu_manager SFU 管理器引用
    /// @return true 初始化成功
    bool init(uint16_t port, RoomManager& room_manager, SFUManager& sfu_manager);

    /// 启动信令服务器
    /// @return true 启动成功
    bool start();

    /// 停止信令服务器（优雅关闭）
    void stop();

    /// 向指定房间广播消息
    /// @param room_id 房间ID
    /// @param message JSON 消息字符串
    void broadcast_to_room(const std::string& room_id, const std::string& message);

    /// 向指定用户发送消息
    /// @param user_id 用户ID
    /// @param message JSON 消息字符串
    /// @return true 发送成功
    bool send_to_user(const std::string& user_id, const std::string& message);

    /// 获取当前连接数
    size_t connection_count() const;

signals:
    /// 参与者加入信号
    void participantJoined(const QString& room_id,
                           const QString& user_id,
                           const QString& role);

    /// 参与者离开信号
    void participantLeft(const QString& room_id,
                         const QString& user_id);

    /// 音频静音状态变更信号
    void audioToggled(const QString& room_id,
                      const QString& user_id,
                      bool muted);

    /// 提示音播放信号
    void tonePlayed(const QString& room_id,
                    const QString& user_id,
                    const QString& tone_type);

private slots:
    /// DTLS 数据包到达槽函数
    void onReadyRead();

    /// 心跳超时检测槽函数
    void onHeartbeatTimeout();

    /// DTLS 握手超时处理槽函数
    void onHandshakeTimeout();

private:
    /// 处理解密后的消息
    void handle_message(std::shared_ptr<ClientConnection> client, const std::string& raw_message);

    /// 解析信令消息类型
    SignalType parse_signal_type(const json& msg);

    /// 处理 join 消息
    void handle_join(std::shared_ptr<ClientConnection> client, const json& msg);

    /// 处理 leave 消息
    void handle_leave(std::shared_ptr<ClientConnection> client, const json& msg);

    /// 处理 mute 消息
    void handle_mute(std::shared_ptr<ClientConnection> client, const json& msg);

    /// 处理 play_tone 消息
    void handle_play_tone(std::shared_ptr<ClientConnection> client, const json& msg);

    /// 处理心跳消息
    void handle_heartbeat(std::shared_ptr<ClientConnection> client);

    /// 断开客户端连接
    void disconnect_client(const QString& user_id);

    /// 查找或创建客户端连接
    std::shared_ptr<ClientConnection> find_or_create_client(
        const QHostAddress& address, quint16 port);

    /// 获取客户端连接
    std::shared_ptr<ClientConnection> get_client(const QString& user_id);

    /// 构建 JSON 响应消息
    std::string build_response(const std::string& type, const json& data);

    /// 通过 DTLS 加密发送数据到客户端
    bool send_dtls_data(std::shared_ptr<ClientConnection> client, const QByteArray& data);

    /// 加载或生成 DTLS 证书
    bool setup_dtls_certificate();

    // 配置
    uint16_t port_ = 8080;

    // Qt 网络对象
    QUdpSocket* udp_socket_ = nullptr;
    QDtlsClientVerifier* dtls_verifier_ = nullptr;
    QTimer* heartbeat_timer_ = nullptr;
    QTimer* handshake_timer_ = nullptr;

    // DTLS 配置
    QSslConfiguration ssl_config_;

    // 客户端连接管理
    mutable std::mutex clients_mutex_;
    /// user_id -> 连接信息
    QMap<QString, std::shared_ptr<ClientConnection>> clients_;
    /// 对端地址+端口 -> user_id（用于快速查找）
    QMap<QString, QString> peer_to_user_;

    // 外部依赖引用
    RoomManager* room_manager_ = nullptr;
    SFUManager*  sfu_manager_  = nullptr;

    // 心跳超时时间（毫秒）
    static constexpr int HEARTBEAT_TIMEOUT_MS = 30000;
    // 心跳检测间隔（毫秒）
    static constexpr int HEARTBEAT_INTERVAL_MS = 5000;
    // DTLS 握手超时时间（毫秒）
    static constexpr int HANDSHAKE_TIMEOUT_MS = 10000;
};

} // namespace voice::sfu
