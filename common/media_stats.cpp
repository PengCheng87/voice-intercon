#include "media_stats.h"
#include <algorithm>
#include <cmath>
#include <chrono>

MediaStats::MediaStats()
    : last_seq_(0)
    , total_packets_(0)
    , lost_packets_(0)
    , bytes_received_(0)
    , current_mos_(4.5)
    , first_packet_time_(0)
    , last_packet_time_(0)
{
}

MediaStats::~MediaStats() {
}

void MediaStats::on_packet_received(uint32_t seq, uint64_t timestamp, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    if (total_packets_ == 0) {
        first_packet_time_ = now;
        last_seq_ = seq;
    } else {
        if (seq > last_seq_ + 1) {
            lost_packets_ += seq - last_seq_ - 1;
        }
    }

    last_seq_ = seq;
    total_packets_++;
    bytes_received_ += size;
    last_packet_time_ = now;

    double latency = static_cast<double>(now - timestamp);
    if (latency > 0) {
        latency_samples_.push_back(latency);
        if (latency_samples_.size() > 100) {
            latency_samples_.pop_front();
        }
    }

    if (latency_samples_.size() >= 2) {
        double jitter = std::abs(latency_samples_.back() - latency_samples_[latency_samples_.size() - 2]);
        jitter_samples_.push_back(jitter);
        if (jitter_samples_.size() > 100) {
            jitter_samples_.pop_front();
        }
    }

    calculate_mos();
}

void MediaStats::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_seq_ = 0;
    total_packets_ = 0;
    lost_packets_ = 0;
    bytes_received_ = 0;
    latency_samples_.clear();
    jitter_samples_.clear();
    current_mos_ = 4.5;
    first_packet_time_ = 0;
    last_packet_time_ = 0;
}

MediaStatsInfo MediaStats::get_stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    MediaStatsInfo info;

    info.total_packets = total_packets_;
    info.lost_packets = lost_packets_;
    info.bytes_received = bytes_received_;
    info.mos_score = current_mos_;

    if (total_packets_ > 0) {
        info.packet_loss_rate = static_cast<double>(lost_packets_) / (total_packets_ + lost_packets_);
    } else {
        info.packet_loss_rate = 0.0;
    }

    if (!latency_samples_.empty()) {
        double sum = 0.0;
        for (double l : latency_samples_) {
            sum += l;
        }
        info.avg_latency_ms = sum / latency_samples_.size();
    } else {
        info.avg_latency_ms = 0.0;
    }

    if (!jitter_samples_.empty()) {
        double sum = 0.0;
        for (double j : jitter_samples_) {
            sum += j;
        }
        info.jitter_ms = sum / jitter_samples_.size();
    } else {
        info.jitter_ms = 0.0;
    }

    return info;
}

void MediaStats::calculate_mos() {
    double loss_factor = 0.0;
    if (total_packets_ > 0) {
        double loss_rate = static_cast<double>(lost_packets_) / (total_packets_ + lost_packets_);
        loss_factor = loss_rate * 15.0;
    }

    double latency_factor = 0.0;
    if (!latency_samples_.empty()) {
        double avg_latency = 0.0;
        for (double l : latency_samples_) {
            avg_latency += l;
        }
        avg_latency /= latency_samples_.size();
        if (avg_latency > 150) {
            latency_factor = (avg_latency - 150) / 100.0;
        }
    }

    double jitter_factor = 0.0;
    if (!jitter_samples_.empty()) {
        double avg_jitter = 0.0;
        for (double j : jitter_samples_) {
            avg_jitter += j;
        }
        avg_jitter /= jitter_samples_.size();
        if (avg_jitter > 30) {
            jitter_factor = (avg_jitter - 30) / 50.0;
        }
    }

    current_mos_ = 4.5 - loss_factor - latency_factor - jitter_factor;
    if (current_mos_ < 1.0) {
        current_mos_ = 1.0;
    } else if (current_mos_ > 4.5) {
        current_mos_ = 4.5;
    }
}
