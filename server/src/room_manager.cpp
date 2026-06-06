#include "room_manager.h"
#include <spdlog/spdlog.h>

namespace voice::sfu {

bool RoomManager::create_room(const std::string& room_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (rooms_.find(room_id) != rooms_.end()) {
        spdlog::warn("房间已存在: {}", room_id);
        return false;
    }

    Room room;
    room.room_id      = room_id;
    room.created_at   = std::chrono::system_clock::now();
    rooms_[room_id]   = std::move(room);

    spdlog::info("房间已创建: {}", room_id);
    return true;
}

Room* RoomManager::get_room(const std::string& room_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return nullptr;
    }
    return &it->second;
}

const Room* RoomManager::get_room(const std::string& room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool RoomManager::remove_room(const std::string& room_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        spdlog::warn("尝试移除不存在的房间: {}", room_id);
        return false;
    }

    size_t count = it->second.participants.size();
    rooms_.erase(it);

    spdlog::info("房间已移除: {} (含 {} 个参与者)", room_id, count);
    return true;
}

bool RoomManager::add_participant(const std::string& room_id,
                                   const Participant& participant,
                                   int max_participants) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        spdlog::warn("房间不存在，无法添加参与者: room_id={}, user_id={}",
                     room_id, participant.user_id);
        return false;
    }

    Room& room = it->second;

    // 检查参与者是否已在房间中
    if (room.participants.find(participant.user_id) != room.participants.end()) {
        spdlog::warn("参与者已在房间中: room_id={}, user_id={}",
                     room_id, participant.user_id);
        return false;
    }

    // 检查房间人数是否已满
    if (static_cast<int>(room.participants.size()) >= max_participants) {
        spdlog::warn("房间已满，无法添加参与者: room_id={}, 当前={}, 最大={}",
                     room_id, room.participants.size(), max_participants);
        return false;
    }

    room.participants[participant.user_id] = participant;
    spdlog::info("参与者已加入房间: room_id={}, user_id={}, role={}",
                 room_id, participant.user_id,
                 participant.role == ParticipantRole::Doctor ? "doctor" : "host");
    return true;
}

bool RoomManager::remove_participant(const std::string& room_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        spdlog::warn("房间不存在，无法移除参与者: room_id={}, user_id={}", room_id, user_id);
        return false;
    }

    auto pit = it->second.participants.find(user_id);
    if (pit == it->second.participants.end()) {
        spdlog::warn("参与者不在房间中: room_id={}, user_id={}", room_id, user_id);
        return false;
    }

    it->second.participants.erase(pit);
    spdlog::info("参与者已离开房间: room_id={}, user_id={}", room_id, user_id);
    return true;
}

Participant* RoomManager::get_participant(const std::string& room_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return nullptr;
    }

    auto pit = it->second.participants.find(user_id);
    if (pit == it->second.participants.end()) {
        return nullptr;
    }
    return &pit->second;
}

json RoomManager::get_participants_json(const std::string& room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    json participants_array = json::array();

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return participants_array;
    }

    for (const auto& [uid, participant] : it->second.participants) {
        json item;
        item["user_id"]      = participant.user_id;
        item["role"]         = (participant.role == ParticipantRole::Doctor) ? "doctor" : "host";
        item["audio_muted"]  = participant.audio_muted;
        item["audio_enabled"] = participant.audio_enabled;
        participants_array.push_back(item);
    }

    return participants_array;
}

size_t RoomManager::room_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rooms_.size();
}

size_t RoomManager::participant_count(const std::string& room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return 0;
    }
    return it->second.participants.size();
}

bool RoomManager::room_exists(const std::string& room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rooms_.find(room_id) != rooms_.end();
}

} // namespace voice::sfu
