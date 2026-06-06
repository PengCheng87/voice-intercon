#pragma once

#include <cstdint>
#include <vector>
#include <memory>

// 前向声明Opus结构体，避免暴露opus头文件
struct OpusEncoder;
struct OpusDecoder;

namespace voice::media {

/// Opus编码器封装类
class OpusEncoderWrapper {
public:
    OpusEncoderWrapper();
    ~OpusEncoderWrapper();

    // 禁止拷贝
    OpusEncoderWrapper(const OpusEncoderWrapper&) = delete;
    OpusEncoderWrapper& operator=(const OpusEncoderWrapper&) = delete;

    /// 初始化编码器
    /// @param sample_rate 采样率（必须48000）
    /// @param channels 声道数（1=单声道）
    /// @param frame_duration_ms 帧时长毫秒（默认20）
    /// @param bitrate_bps 比特率（默认64000）
    /// @return true表示初始化成功
    bool init(int sample_rate = 48000,
              int channels = 1,
              int frame_duration_ms = 20,
              int bitrate_bps = 64000);

    /// 释放编码器资源
    void cleanup();

    /// 编码PCM音频数据
    /// @param pcm 输入PCM数据（float格式，范围[-1.0, 1.0]）
    /// @param samples 采样数（如20ms@48kHz=960个采样）
    /// @return 编码后的Opus数据，失败返回空vector
    std::vector<uint8_t> encode(const float* pcm, int samples);

    /// 编码PCM音频数据（int16版本）
    /// @param pcm 输入PCM数据（int16格式）
    /// @param samples 采样数
    /// @return 编码后的Opus数据，失败返回空vector
    std::vector<uint8_t> encode(const int16_t* pcm, int samples);

    /// 获取每帧采样数
    int frame_samples() const { return frame_samples_; }

    /// 是否已初始化
    bool is_initialized() const { return encoder_ != nullptr; }

private:
    OpusEncoder* encoder_ = nullptr;
    int sample_rate_ = 48000;
    int channels_ = 1;
    int frame_duration_ms_ = 20;
    int frame_samples_ = 960;  // 48000 * 20 / 1000
    int bitrate_bps_ = 64000;

    // 编码缓冲区（最大允许4000字节每帧）
    std::vector<uint8_t> encode_buffer_;
};

/// Opus解码器封装类
class OpusDecoderWrapper {
public:
    OpusDecoderWrapper();
    ~OpusDecoderWrapper();

    // 禁止拷贝
    OpusDecoderWrapper(const OpusDecoderWrapper&) = delete;
    OpusDecoderWrapper& operator=(const OpusDecoderWrapper&) = delete;

    /// 初始化解码器
    /// @param sample_rate 采样率（必须48000）
    /// @param channels 声道数（1=单声道）
    /// @param frame_duration_ms 帧时长毫秒（默认20）
    /// @return true表示初始化成功
    bool init(int sample_rate = 48000,
              int channels = 1,
              int frame_duration_ms = 20);

    /// 释放解码器资源
    void cleanup();

    /// 解码Opus数据为PCM（float格式）
    /// @param data 输入Opus数据
    /// @param len 数据长度
    /// @param samples_per_frame 每帧期望采样数
    /// @return 解码后的PCM数据（float格式），失败返回空vector
    std::vector<float> decode(const uint8_t* data, int len, int samples_per_frame);

    /// 解码Opus数据为PCM（int16格式）
    /// @param data 输入Opus数据
    /// @param len 数据长度
    /// @param samples_per_frame 每帧期望采样数
    /// @return 解码后的PCM数据（int16格式），失败返回空vector
    std::vector<int16_t> decode_int16(const uint8_t* data, int len, int samples_per_frame);

    /// 获取每帧采样数
    int frame_samples() const { return frame_samples_; }

    /// 是否已初始化
    bool is_initialized() const { return decoder_ != nullptr; }

private:
    OpusDecoder* decoder_ = nullptr;
    int sample_rate_ = 48000;
    int channels_ = 1;
    int frame_duration_ms_ = 20;
    int frame_samples_ = 960;

    // 解码缓冲区
    std::vector<float> decode_buffer_float_;
    std::vector<int16_t> decode_buffer_int16_;
};

} // namespace voice::media
