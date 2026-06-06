#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <mutex>

struct MediaStatsInfo {
    double packet_loss_rate;
    double avg_latency_ms;
    double jitter_ms;
    double mos_score;
    uint64_t total_packets;
    uint64_t lost_packets;
    uint64_t bytes_received;
};

class MediaStats {
public:
    MediaStats();
    ~MediaStats();

    void on_packet_received(uint32_t seq, uint64_t timestamp, size_t size);
    void reset();
    MediaStatsInfo get_stats();

private:
    void calculate_mos();

    std::mutex mutex_;
    uint32_t last_seq_;
    uint64_t total_packets_;
    uint64_t lost_packets_;
    uint64_t bytes_received_;
    std::deque<double> latency_samples_;
    std::deque<double> jitter_samples_;
    double current_mos_;
    uint64_t first_packet_time_;
    uint64_t last_packet_time_;
};
