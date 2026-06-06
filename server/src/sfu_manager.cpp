#include "sfu_manager.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <chrono>

namespace voice::sfu {

SFUManager::SFUManager() = default;

SFUManager::~SFUManager() {
    shutdown();
}

void SFUManager::init(uint16_t media_port_min, uint16_t media_port_max) {
    media_port_min_ = media_port_min;
    media_port_max_ = media_port_max;
    next_port_      = media_port_min;
    running_.store(true);

    // 启动处理线程（TODO: 集成 WebRTC 媒体层后负责实际 RTP 转发）
    processing_thread_ = std::thread(&SFUManager::processing_loop, this);

    spdlog::info("SFU 管理器已初始化，媒体端口范围: {}-{}", media_port_min, media_port_max);
}

void SFUManager::shutdown() {
    if (!running_.exchange(false)) {
        return;
    }

    spdlog::info("SFU 管理器正在关闭...");

    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }

    // 清理所有传输
    {
        std::lock_guard<std::mutex> lock(transport_mutex_);
        transports_.clear();
    }

    // 清理所有生产者
    {
        std::lock_guard<std::mutex> lock(producer_mutex_);
        producers_.clear();
    }

    // 清理所有消费者
    {
        std::lock_guard<std::mutex> lock(consumer_mutex_);
        consumers_.clear();
    }

    spdlog::info("SFU 管理器已关闭");
}

TransportInfo SFUManager::create_transport(const std::string& room_id,
                                           const std::string& participant_id) {
    TransportInfo info;
    info.transport_id   = generate_id("transport");
    info.port           = allocate_port();
    info.ice_ufrag      = generate_id("ufrag");
    info.ice_password   = generate_id("pwd");

    info.ice_candidates = "[]"; // TODO: 生成真实 ICE 候选

    std::lock_guard<std::mutex> lock(transport_mutex_);
    transports_[room_id][participant_id] = info;

    spdlog::info("已创建传输通道: room_id={}, participant_id={}, transport_id={}, port={}",
                 room_id, participant_id, info.transport_id, info.port);

    return info;
}

std::string SFUManager::add_producer(const std::string& room_id,
                                     const std::string& participant_id,
                                     MediaType media_type) {
    std::string producer_id = generate_id("producer");

    ProducerInfo info;
    info.producer_id    = producer_id;
    info.participant_id = participant_id;
    info.media_type     = media_type;

    std::lock_guard<std::mutex> lock(producer_mutex_);
    producers_[room_id][producer_id] = info;

    spdlog::info("已添加生产者: room_id={}, participant_id={}, producer_id={}, media_type={}",
                 room_id, participant_id, producer_id,
                 media_type == MediaType::Audio ? "audio" : "video");

    return producer_id; // TODO: 创建实际的 WebRTC RTP Producer
}

void SFUManager::remove_producer(const std::string& room_id,
                                 const std::string& participant_id) {
    std::lock_guard<std::mutex> lock(producer_mutex_);

    auto room_it = producers_.find(room_id);
    if (room_it == producers_.end()) {
        return;
    }

    // 查找并移除该参与者的所有生产者
    for (auto it = room_it->second.begin(); it != room_it->second.end(); ) {
        if (it->second.participant_id == participant_id) {
            spdlog::info("已移除生产者: room_id={}, participant_id={}, producer_id={}",
                         room_id, participant_id, it->second.producer_id);
            it = room_it->second.erase(it); // TODO: 关闭 WebRTC RTP Producer
        } else {
            ++it;
        }
    }
}

std::string SFUManager::add_consumer(const std::string& room_id,
                                     const std::string& participant_id,
                                     const std::string& producer_id) {
    std::string consumer_id = generate_id("consumer");

    ConsumerInfo info;
    info.consumer_id    = consumer_id;
    info.participant_id = participant_id;
    info.producer_id    = producer_id;

    std::lock_guard<std::mutex> lock(consumer_mutex_);
    consumers_[room_id][participant_id] = info;

    spdlog::info("已添加消费者: room_id={}, participant_id={}, consumer_id={}, producer_id={}",
                 room_id, participant_id, consumer_id, producer_id);

    return consumer_id; // TODO: 创建实际的 WebRTC RTP Consumer
}

void SFUManager::remove_consumer(const std::string& room_id,
                                  const std::string& participant_id) {
    std::lock_guard<std::mutex> lock(consumer_mutex_);

    auto room_it = consumers_.find(room_id);
    if (room_it == consumers_.end()) {
        return;
    }

    auto it = room_it->second.find(participant_id);
    if (it != room_it->second.end()) {
        spdlog::info("已移除消费者: room_id={}, participant_id={}, consumer_id={}",
                     room_id, participant_id, it->second.consumer_id);
        room_it->second.erase(it); // TODO: 关闭 WebRTC RTP Consumer
    }
}

json SFUManager::get_producers_json(const std::string& room_id) const {
    std::lock_guard<std::mutex> lock(producer_mutex_);

    json array = json::array();

    auto room_it = producers_.find(room_id);
    if (room_it == producers_.end()) {
        return array;
    }

    for (const auto& [pid, info] : room_it->second) {
        json item;
        item["producer_id"]    = info.producer_id;
        item["participant_id"] = info.participant_id;
        item["media_type"]     = (info.media_type == MediaType::Audio) ? "audio" : "video";
        array.push_back(item);
    }

    return array;
}

void SFUManager::remove_participant_transports(const std::string& room_id,
                                               const std::string& participant_id) {
    // 移除传输
    {
        std::lock_guard<std::mutex> lock(transport_mutex_);
        auto room_it = transports_.find(room_id);
        if (room_it != transports_.end()) {
            room_it->second.erase(participant_id);
            spdlog::info("已移除传输通道: room_id={}, participant_id={}", room_id, participant_id);
        }
    }

    // 移除生产者
    remove_producer(room_id, participant_id);

    // 移除消费者
    remove_consumer(room_id, participant_id);
}

std::string SFUManager::generate_id(const std::string& prefix) {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << prefix << "-" << now << "-" << counter.fetch_add(1);
    return oss.str();
}

uint16_t SFUManager::allocate_port() {
    std::lock_guard<std::mutex> lock(port_mutex_);

    uint16_t port = next_port_;
    next_port_++;

    // 端口循环使用
    if (next_port_ > media_port_max_) {
        next_port_ = media_port_min_;
    }

    return port;
}

void SFUManager::processing_loop() {
    spdlog::info("SFU 处理线程已启动");

    while (running_.load()) {
        // TODO: 集成 QUdpSocket + QDtls 实现 RTP 媒体转发
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::info("SFU 处理线程已退出");
}

} // namespace voice::sfu
