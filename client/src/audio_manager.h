#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdint>
#include <functional>

namespace voice::client {

/// 音频设备信息
struct AudioDeviceInfo {
    int         index;          ///< 设备索引
    std::string name;           ///< 设备名称
    int         max_channels;   ///< 最大声道数
    int         max_sample_rate; ///< 最大采样率
};

/// 音频帧回调（采集到的音频数据）
using AudioFrameCallback = std::function<void(const std::int16_t* data, std::size_t frames)>;

/// 音频管理器 - 负责音频采集、播放、编解码
/// 当前为骨架实现，预留Qt Multimedia音频接口
class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // 禁止拷贝
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    /// 初始化音频设备
    /// @param input_device  输入设备名称（"default"为默认设备）
    /// @param output_device 输出设备名称
    /// @param sample_rate   采样率
    /// @param channels      声道数
    /// @return true表示初始化成功
    bool init(const std::string& input_device,
              const std::string& output_device,
              int sample_rate,
              int channels);

    /// 释放音频资源
    void cleanup();

    /// 开始音频采集
    bool start_capture();

    /// 停止音频采集
    bool stop_capture();

    /// 开始音频播放
    bool start_playback();

    /// 停止音频播放
    bool stop_playback();

    /// 设置静音（停止发送音频）
    void set_mute(bool muted);

    /// 获取静音状态
    bool is_muted() const;

    /// 设置停止收听（停止播放接收到的音频）
    void set_silence(bool silenced);

    /// 获取收听状态
    bool is_silenced() const;

    /// 获取当前音频电平（0.0 ~ 1.0）
    float get_audio_level() const;

    /// 设置音频帧回调（采集到数据时调用）
    void set_capture_callback(AudioFrameCallback callback);

    /// 输入音频数据用于播放（从网络接收的音频）
    void feed_playback_data(const std::int16_t* data, std::size_t frames);

    /// 获取可用输入设备列表
    static std::vector<AudioDeviceInfo> get_input_devices();

    /// 获取可用输出设备列表
    static std::vector<AudioDeviceInfo> get_output_devices();

    /// 是否已初始化
    bool is_initialized() const;

    /// 是否正在采集
    bool is_capturing() const;

    /// 是否正在播放
    bool is_playing() const;

    // Opus 编解码（骨架实现，待集成 OpusEncoderWrapper/OpusDecoderWrapper）
    std::vector<std::uint8_t> encode_audio(const std::int16_t* pcm_data, std::size_t frame_count);
    std::vector<std::int16_t> decode_audio(const std::uint8_t* encoded_data, std::size_t data_size);

private:
    /// 采集线程主函数
    void capture_thread_func();

    /// 播放线程主函数
    void playback_thread_func();

    /// 计算音频电平
    static float calculate_level(const std::int16_t* data, std::size_t frames);

    // 配置
    std::string input_device_name_;
    std::string output_device_name_;
    int         sample_rate_  = 48000;
    int         channels_     = 1;

    // 状态
    std::atomic<bool> initialized_{false};
    std::atomic<bool> capturing_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> muted_{false};
    std::atomic<bool> silenced_{false};

    // 音频电平
    std::atomic<float> audio_level_{0.0f};

    // 采集回调
    AudioFrameCallback capture_callback_;

    // 采集线程
    std::thread capture_thread_;

    // 播放线程
    std::thread playback_thread_;

    // 播放缓冲区（线程安全）
    mutable std::mutex playback_mutex_;
    std::vector<std::int16_t> playback_buffer_;
    static constexpr std::size_t PLAYBACK_BUFFER_MAX = 48000 * 2; // 2秒缓冲
};

} // namespace voice::client
