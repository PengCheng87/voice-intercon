#include "audio_manager.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <cmath>
#include <algorithm>

namespace voice::client {

// ============================================================
// 构造/析构
// ============================================================
AudioManager::AudioManager() {
    spdlog::debug("音频管理器已创建");
}

AudioManager::~AudioManager() {
    cleanup();
    spdlog::debug("音频管理器已销毁");
}

// ============================================================
// 初始化
// ============================================================
bool AudioManager::init(const std::string& input_device,
                        const std::string& output_device,
                        int sample_rate,
                        int channels) {
    if (initialized_) {
        spdlog::warn("音频管理器已经初始化");
        return true;
    }

    spdlog::info("正在初始化音频管理器...");
    spdlog::info("  输入设备: {}", input_device);
    spdlog::info("  输出设备: {}", output_device);
    spdlog::info("  采样率: {} Hz", sample_rate);
    spdlog::info("  声道数: {}", channels);

    input_device_name_  = input_device;
    output_device_name_ = output_device;
    sample_rate_        = sample_rate;
    channels_           = channels;

    // TODO: 接入 Qt Multimedia (QAudioInput/QAudioOutput) 实现真实音频采集和播放
    // TODO: 集成 Opus 编解码器 (OpusEncoderWrapper/OpusDecoderWrapper)

    initialized_ = true;
    spdlog::info("音频管理器初始化完成");
    return true;
}

// ============================================================
// 释放资源
// ============================================================
void AudioManager::cleanup() {
    if (!initialized_) return;

    stop_capture();
    stop_playback();

    // TODO: 释放 Qt Multimedia 和 Opus 编解码器资源

    initialized_ = false;
    spdlog::info("音频管理器资源已释放");
}

// ============================================================
// 开始采集
// ============================================================
bool AudioManager::start_capture() {
    if (!initialized_) {
        spdlog::error("音频管理器未初始化，无法开始采集");
        return false;
    }
    if (capturing_) {
        return true;
    }

    capturing_ = true;
    capture_thread_ = std::thread(&AudioManager::capture_thread_func, this);

    spdlog::info("音频采集已开始");
    return true;
}

// ============================================================
// 停止采集
// ============================================================
bool AudioManager::stop_capture() {
    if (!capturing_) return true;

    capturing_ = false;

    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }

    spdlog::info("音频采集已停止");
    return true;
}

// ============================================================
// 开始播放
// ============================================================
bool AudioManager::start_playback() {
    if (!initialized_) {
        spdlog::error("音频管理器未初始化，无法开始播放");
        return false;
    }
    if (playing_) return true;

    playing_ = true;
    playback_thread_ = std::thread(&AudioManager::playback_thread_func, this);

    spdlog::info("音频播放已开始");
    return true;
}

// ============================================================
// 停止播放
// ============================================================
bool AudioManager::stop_playback() {
    if (!playing_) return true;

    playing_ = false;

    if (playback_thread_.joinable()) {
        playback_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        playback_buffer_.clear();
    }

    spdlog::info("音频播放已停止");
    return true;
}

// ============================================================
// 静音控制
// ============================================================
void AudioManager::set_mute(bool muted) {
    muted_ = muted;
    spdlog::info("静音状态: {}", muted ? "已静音" : "已取消静音");
}

bool AudioManager::is_muted() const {
    return muted_;
}

// ============================================================
// 收听控制
// ============================================================
void AudioManager::set_silence(bool silenced) {
    silenced_ = silenced;
    spdlog::info("收听状态: {}", silenced ? "已停止收听" : "已恢复收听");
}

bool AudioManager::is_silenced() const {
    return silenced_;
}

// ============================================================
// 音频电平
// ============================================================
float AudioManager::get_audio_level() const {
    return audio_level_.load();
}

// ============================================================
// 采集回调设置
// ============================================================
void AudioManager::set_capture_callback(AudioFrameCallback callback) {
    capture_callback_ = std::move(callback);
}

// ============================================================
// 播放数据输入
// ============================================================
void AudioManager::feed_playback_data(const std::int16_t* data, std::size_t frames) {
    if (!playing_ || silenced_) return;

    std::lock_guard<std::mutex> lock(playback_mutex_);
    std::size_t samples = frames * static_cast<std::size_t>(channels_);

    // 防止缓冲区溢出
    if (playback_buffer_.size() + samples > PLAYBACK_BUFFER_MAX) {
        // 丢弃旧数据
        std::size_t overflow = playback_buffer_.size() + samples - PLAYBACK_BUFFER_MAX;
        if (overflow < playback_buffer_.size()) {
            playback_buffer_.erase(playback_buffer_.begin(),
                                    playback_buffer_.begin() + static_cast<std::ptrdiff_t>(overflow));
        } else {
            playback_buffer_.clear();
        }
    }

    playback_buffer_.insert(playback_buffer_.end(), data, data + samples);
}

// ============================================================
// 获取设备列表
// ============================================================
std::vector<AudioDeviceInfo> AudioManager::get_input_devices() {
    // TODO: 接入 Qt Multimedia QAudioDeviceInfo::availableDevices 枚举
    return {{0, "default", 1, 48000}};
}

std::vector<AudioDeviceInfo> AudioManager::get_output_devices() {
    // TODO: 接入 Qt Multimedia QAudioDeviceInfo::availableDevices 枚举
    return {{0, "default", 1, 48000}};
}

// ============================================================
// 状态查询
// ============================================================
bool AudioManager::is_initialized() const { return initialized_; }
bool AudioManager::is_capturing() const   { return capturing_; }
bool AudioManager::is_playing() const    { return playing_; }

// ============================================================
// 音频编解码（骨架：直通PCM，待接入 Opus）
// ============================================================
std::vector<std::uint8_t> AudioManager::encode_audio(const std::int16_t* pcm_data, std::size_t frame_count) {
    // TODO: 替换为 OpusEncoderWrapper::encode()
    std::vector<std::uint8_t> encoded;
    std::size_t byte_count = frame_count * static_cast<std::size_t>(channels_) * sizeof(std::int16_t);
    encoded.resize(byte_count);
    std::memcpy(encoded.data(), pcm_data, byte_count);
    return encoded;
}

std::vector<std::int16_t> AudioManager::decode_audio(const std::uint8_t* encoded_data, std::size_t data_size) {
    // TODO: 替换为 OpusDecoderWrapper::decode_int16()
    std::size_t sample_count = data_size / sizeof(std::int16_t);
    std::vector<std::int16_t> pcm(sample_count);
    std::memcpy(pcm.data(), encoded_data, data_size);
    return pcm;
}

void AudioManager::capture_thread_func() {
    spdlog::debug("音频采集线程已启动");

    constexpr int frames_per_buffer = 960; // 20ms @ 48kHz
    std::vector<std::int16_t> buffer(frames_per_buffer * channels_, 0);

    while (capturing_) {
        // TODO: 从 Qt Multimedia QAudioInput 读取真实音频数据
        if (muted_) {
            std::fill(buffer.begin(), buffer.end(), 0);
        }

        audio_level_.store(calculate_level(buffer.data(), frames_per_buffer));

        if (capture_callback_ && !muted_) {
            capture_callback_(buffer.data(), frames_per_buffer);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    spdlog::debug("音频采集线程已退出");
}

void AudioManager::playback_thread_func() {
    spdlog::debug("音频播放线程已启动");

    constexpr int frames_per_buffer = 960;

    while (playing_) {
        std::vector<std::int16_t> buffer(frames_per_buffer * channels_, 0);

        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            std::size_t needed = static_cast<std::size_t>(frames_per_buffer) * channels_;
            if (playback_buffer_.size() >= needed) {
                std::copy(playback_buffer_.begin(),
                          playback_buffer_.begin() + static_cast<std::ptrdiff_t>(needed),
                          buffer.begin());
                playback_buffer_.erase(playback_buffer_.begin(),
                                       playback_buffer_.begin() + static_cast<std::ptrdiff_t>(needed));
            } else if (!playback_buffer_.empty()) {
                std::copy(playback_buffer_.begin(), playback_buffer_.end(), buffer.begin());
                playback_buffer_.clear();
            }
        }

        if (silenced_) {
            std::fill(buffer.begin(), buffer.end(), 0);
        }

        // TODO: 通过 Qt Multimedia QAudioOutput 写入扬声器

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    spdlog::debug("音频播放线程已退出");
}

// ============================================================
// 计算音频电平（RMS）
// ============================================================
float AudioManager::calculate_level(const std::int16_t* data, std::size_t frames) {
    if (!data || frames == 0) return 0.0f;

    double sum = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        double sample = static_cast<double>(data[i]) / 32768.0;
        sum += sample * sample;
    }

    double rms = std::sqrt(sum / static_cast<double>(frames));
    return static_cast<float>(std::min(rms * 2.0, 1.0)); // 放大并限制在0~1
}

} // namespace voice::client
