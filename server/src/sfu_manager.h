#pragma once

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>

namespace voice::sfu {

using json = nlohmann::json;

/// 媒体类型
enum class MediaType {
    Audio,  ///< 音频
    Video   ///< 视频（预留）
};

/// 传输信息
struct TransportInfo {
    std::string transport_id;    ///< 传输ID
    uint16_t    port;           ///< 媒体端口
    std::string ice_ufrag;      ///< ICE 用户名片段
    std::string ice_password;   ///< ICE 密码
    std::string ice_candidates; ///< ICE 候选列表（JSON 字符串）
};

/// 生产者信息
struct ProducerInfo {
    std::string producer_id;    ///< 生产者ID
    std::string participant_id; ///< 所属参与者ID
    MediaType   media_type;     ///< 媒体类型
};

/// 消费者信息
struct ConsumerInfo {
    std::string consumer_id;    ///< 消费者ID
    std::string participant_id; ///< 所属参与者ID
    std::string producer_id;    ///< 订阅的生产者ID
};

/// SFU 媒体管理器（骨架：管理传输/生产者/消费者元数据，待集成实际 RTP 转发）
class SFUManager {
public:
    SFUManager();
    ~SFUManager();

    // 禁止拷贝
    SFUManager(const SFUManager&) = delete;
    SFUManager& operator=(const SFUManager&) = delete;

    /// 初始化 SFU 管理器
    /// @param media_port_min 媒体端口最小值
    /// @param media_port_max 媒体端口最大值
    void init(uint16_t media_port_min, uint16_t media_port_max);

    /// 关闭 SFU 管理器，释放所有资源
    void shutdown();

    /// 为参与者创建传输通道
    /// @param room_id 房间ID
    /// @param participant_id 参与者ID
    /// @return 传输信息
    TransportInfo create_transport(const std::string& room_id,
                                   const std::string& participant_id);

    /// 添加媒体生产者（上行流）
    /// @param room_id 房间ID
    /// @param participant_id 参与者ID
    /// @param media_type 媒体类型
    /// @return 生产者ID
    std::string add_producer(const std::string& room_id,
                             const std::string& participant_id,
                             MediaType media_type);

    /// 移除媒体生产者
    /// @param room_id 房间ID
    /// @param participant_id 参与者ID
    void remove_producer(const std::string& room_id,
                         const std::string& participant_id);

    /// 添加媒体消费者（下行流）
    /// @param room_id 房间ID
    /// @param participant_id 参与者ID
    /// @param producer_id 要订阅的生产者ID
    /// @return 消费者ID
    std::string add_consumer(const std::string& room_id,
                             const std::string& participant_id,
                             const std::string& producer_id);

    /// 移除媒体消费者
    /// @param room_id 房间ID
    /// @param participant_id 参与者ID
    void remove_consumer(const std::string& room_id,
                         const std::string& participant_id);

    /// 获取房间的所有生产者列表
    /// @param room_id 房间ID
    /// @return 生产者列表 JSON
    json get_producers_json(const std::string& room_id) const;

    /// 移除指定参与者的所有传输
    /// @param room_id 房间ID
    /// @param participant_id 参与者ID
    void remove_participant_transports(const std::string& room_id,
                                       const std::string& participant_id);

private:
    /// 生成唯一ID
    std::string generate_id(const std::string& prefix);

    /// 分配媒体端口
    uint16_t allocate_port();

    /// 异步处理线程函数
    void processing_loop();

    // 配置
    uint16_t media_port_min_ = 4000;
    uint16_t media_port_max_ = 4010;

    // 状态
    std::atomic<bool> running_{false};
    std::thread processing_thread_;

    // 端口分配
    std::mutex port_mutex_;
    uint16_t   next_port_ = 0;

    // 传输管理 room_id -> participant_id -> TransportInfo
    mutable std::mutex transport_mutex_;
    std::map<std::string, std::map<std::string, TransportInfo>> transports_;

    // 生产者管理 room_id -> producer_id -> ProducerInfo
    mutable std::mutex producer_mutex_;
    std::map<std::string, std::map<std::string, ProducerInfo>> producers_;

    // 消费者管理 room_id -> participant_id -> ConsumerInfo
    mutable std::mutex consumer_mutex_;
    std::map<std::string, std::map<std::string, ConsumerInfo>> consumers_;
};

} // namespace voice::sfu
