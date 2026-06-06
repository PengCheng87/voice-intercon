#pragma once

#include <string>
#include <map>
#include <chrono>
#include <mutex>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>

// DTLS 模式下不使用原生 WebSocket 句柄，此处保留为 void* 以兼容现有结构
// 实际连接管理由 SignalingServer 通过 QDtls + peer 地址维护
using WebSocketHandle = void*;

namespace voice::sfu {

using json = nlohmann::json;

/// 参与者角色
enum class ParticipantRole {
    Doctor,  ///< 医生
    Host     ///< 主持人
};

/// 参与者信息
struct Participant {
    std::string              user_id;        ///< 用户唯一标识
    ParticipantRole          role;           ///< 角色
    bool                     audio_muted     = false;  ///< 是否静音
    bool                     audio_enabled   = true;   ///< 音频是否启用
    std::chrono::system_clock::time_point connected_at; ///< 连接时间
    WebSocketHandle          ws_handle       = nullptr;  ///< WebSocket 连接句柄
};

/// 房间信息
struct Room {
    std::string                                    room_id;       ///< 房间唯一标识
    std::map<std::string, Participant>             participants;  ///< 参与者列表（key 为 user_id）
    std::chrono::system_clock::time_point          created_at;    ///< 房间创建时间
};

/// 房间管理器
/// 负责房间的创建、销毁以及参与者的管理
class RoomManager {
public:
    RoomManager() = default;
    ~RoomManager() = default;

    // 禁止拷贝
    RoomManager(const RoomManager&) = delete;
    RoomManager& operator=(const RoomManager&) = delete;

    /// 创建房间
    /// @param room_id 房间ID
    /// @return true 创建成功，false 房间已存在
    bool create_room(const std::string& room_id);

    /// 获取房间
    /// @param room_id 房间ID
    /// @return 房间指针，不存在返回 nullptr
    Room* get_room(const std::string& room_id);

    /// 获取房间（const 版本）
    const Room* get_room(const std::string& room_id) const;

    /// 移除房间
    /// @param room_id 房间ID
    /// @return true 移除成功，false 房间不存在
    bool remove_room(const std::string& room_id);

    /// 向房间添加参与者
    /// @param room_id 房间ID
    /// @param participant 参与者信息
    /// @param max_participants 房间最大参与者数
    /// @return true 添加成功，false 房间不存在或已满
    bool add_participant(const std::string& room_id,
                         const Participant& participant,
                         int max_participants = 16);

    /// 从房间移除参与者
    /// @param room_id 房间ID
    /// @param user_id 用户ID
    /// @return true 移除成功，false 参与者不存在
    bool remove_participant(const std::string& room_id, const std::string& user_id);

    /// 获取参与者
    /// @param room_id 房间ID
    /// @param user_id 用户ID
    /// @return 参与者指针，不存在返回 nullptr
    Participant* get_participant(const std::string& room_id, const std::string& user_id);

    /// 获取房间参与者列表的 JSON 表示
    /// @param room_id 房间ID
    /// @return json 数组，包含所有参与者信息
    json get_participants_json(const std::string& room_id) const;

    /// 获取房间数量
    size_t room_count() const;

    /// 获取指定房间的参与者数量
    size_t participant_count(const std::string& room_id) const;

    /// 检查房间是否存在
    bool room_exists(const std::string& room_id) const;

private:
    mutable std::mutex mutex_;                              ///< 互斥锁
    std::map<std::string, Room> rooms_;                    ///< 房间列表
};

} // namespace voice::sfu
