#pragma once

#include <unordered_map>
#include <vector>
#include <mutex>
#include <queue>
#include <cstdint>
#include <cstddef>

enum class Priority {
    P0_Alarm = 0,
    P1_Voice = 1,
    P2_Prompt = 2
};

struct StreamInfo {
    int stream_id;
    Priority priority;
    std::vector<float> buffer;
    bool active;
};

class PriorityMixer {
public:
    PriorityMixer();
    ~PriorityMixer();

    void add_stream(int stream_id, Priority priority);
    void remove_stream(int stream_id);
    void write_stream(int stream_id, const float* pcm, size_t frames);
    void mix(float* output, size_t frames);
    void clear();

private:
    std::mutex mutex_;
    std::unordered_map<int, StreamInfo> streams_;
    int active_p0_stream_;
    size_t frame_size_;
};
