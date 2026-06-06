#include "priority_mixer.h"
#include <algorithm>
#include <cstring>

PriorityMixer::PriorityMixer()
    : active_p0_stream_(-1)
    , frame_size_(960) // 20ms @ 48kHz
{
}

PriorityMixer::~PriorityMixer() {
    clear();
}

void PriorityMixer::add_stream(int stream_id, Priority priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    StreamInfo info;
    info.stream_id = stream_id;
    info.priority = priority;
    info.buffer.resize(frame_size_);
    info.active = false;
    streams_[stream_id] = std::move(info);
}

void PriorityMixer::remove_stream(int stream_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    streams_.erase(stream_id);
    if (active_p0_stream_ == stream_id) {
        active_p0_stream_ = -1;
    }
}

void PriorityMixer::write_stream(int stream_id, const float* pcm, size_t frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        size_t copy_frames = std::min(frames, frame_size_);
        std::memcpy(it->second.buffer.data(), pcm, copy_frames * sizeof(float));
        it->second.active = true;

        if (it->second.priority == Priority::P0_Alarm) {
            active_p0_stream_ = stream_id;
        }
    }
}

void PriorityMixer::mix(float* output, size_t frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t mix_frames = std::min(frames, frame_size_);
    std::memset(output, 0, mix_frames * sizeof(float));

    if (active_p0_stream_ >= 0) {
        auto it = streams_.find(active_p0_stream_);
        if (it != streams_.end() && it->second.active) {
            std::memcpy(output, it->second.buffer.data(), mix_frames * sizeof(float));
            it->second.active = false;
            active_p0_stream_ = -1;
            return;
        }
    }

    bool has_p1 = false;
    for (auto& pair : streams_) {
        if (pair.second.priority == Priority::P1_Voice && pair.second.active) {
            has_p1 = true;
            for (size_t i = 0; i < mix_frames; ++i) {
                output[i] += pair.second.buffer[i];
            }
            pair.second.active = false;
        }
    }

    if (!has_p1) {
        for (auto& pair : streams_) {
            if (pair.second.priority == Priority::P2_Prompt && pair.second.active) {
                for (size_t i = 0; i < mix_frames; ++i) {
                    output[i] += pair.second.buffer[i];
                }
                pair.second.active = false;
                break;
            }
        }
    }

    float max_amp = 1.0f;
    for (size_t i = 0; i < mix_frames; ++i) {
        if (std::abs(output[i]) > max_amp) {
            max_amp = std::abs(output[i]);
        }
    }
    if (max_amp > 1.0f) {
        for (size_t i = 0; i < mix_frames; ++i) {
            output[i] /= max_amp;
        }
    }
}

void PriorityMixer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    streams_.clear();
    active_p0_stream_ = -1;
}
