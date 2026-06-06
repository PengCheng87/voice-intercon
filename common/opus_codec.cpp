#include "opus_codec.h"
#include <opus/opus.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>

namespace voice::media {

// ============================================================
// OpusEncoderWrapper
// ============================================================

OpusEncoderWrapper::OpusEncoderWrapper() = default;

OpusEncoderWrapper::~OpusEncoderWrapper() {
    cleanup();
}

bool OpusEncoderWrapper::init(int sample_rate, int channels,
                               int frame_duration_ms, int bitrate_bps) {
    if (encoder_) {
        spdlog::warn("Opus编码器已初始化，先清理再重新初始化");
        cleanup();
    }

    sample_rate_ = sample_rate;
    channels_ = channels;
    frame_duration_ms_ = frame_duration_ms;
    bitrate_bps_ = bitrate_bps;
    frame_samples_ = sample_rate * frame_duration_ms / 1000;

    int error = 0;
    encoder_ = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK || encoder_ == nullptr) {
        spdlog::error("Opus编码器创建失败: {}", opus_strerror(error));
        encoder_ = nullptr;
        return false;
    }

    // 设置比特率
    opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate_bps));
    // 启用FEC（前向纠错），提升弱网体验
    opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(1));
    // 启用DTX（不连续传输），静音时减少带宽
    opus_encoder_ctl(encoder_, OPUS_SET_DTX(1));

    // 分配编码缓冲区（Opus单帧最大约4000字节）
    encode_buffer_.resize(4000);

    spdlog::info("Opus编码器初始化成功: sample_rate={}, channels={}, frame_ms={}, bitrate={}",
                 sample_rate, channels, frame_duration_ms, bitrate_bps);
    return true;
}

void OpusEncoderWrapper::cleanup() {
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
        spdlog::debug("Opus编码器已释放");
    }
    encode_buffer_.clear();
}

std::vector<uint8_t> OpusEncoderWrapper::encode(const float* pcm, int samples) {
    if (!encoder_) {
        spdlog::error("Opus编码器未初始化");
        return {};
    }
    if (!pcm || samples != frame_samples_) {
        spdlog::error("Opus编码输入参数错误: samples={}, expected={}", samples, frame_samples_);
        return {};
    }

    int encoded_bytes = opus_encode_float(encoder_, pcm, samples,
                                          encode_buffer_.data(),
                                          static_cast<opus_int32>(encode_buffer_.size()));
    if (encoded_bytes < 0) {
        spdlog::error("Opus编码失败: {}", opus_strerror(encoded_bytes));
        return {};
    }

    return std::vector<uint8_t>(encode_buffer_.begin(),
                                encode_buffer_.begin() + encoded_bytes);
}

std::vector<uint8_t> OpusEncoderWrapper::encode(const int16_t* pcm, int samples) {
    if (!encoder_) {
        spdlog::error("Opus编码器未初始化");
        return {};
    }
    if (!pcm || samples != frame_samples_) {
        spdlog::error("Opus编码输入参数错误: samples={}, expected={}", samples, frame_samples_);
        return {};
    }

    int encoded_bytes = opus_encode(encoder_, pcm, samples,
                                    encode_buffer_.data(),
                                    static_cast<opus_int32>(encode_buffer_.size()));
    if (encoded_bytes < 0) {
        spdlog::error("Opus编码失败: {}", opus_strerror(encoded_bytes));
        return {};
    }

    return std::vector<uint8_t>(encode_buffer_.begin(),
                                encode_buffer_.begin() + encoded_bytes);
}

// ============================================================
// OpusDecoderWrapper
// ============================================================

OpusDecoderWrapper::OpusDecoderWrapper() = default;

OpusDecoderWrapper::~OpusDecoderWrapper() {
    cleanup();
}

bool OpusDecoderWrapper::init(int sample_rate, int channels, int frame_duration_ms) {
    if (decoder_) {
        spdlog::warn("Opus解码器已初始化，先清理再重新初始化");
        cleanup();
    }

    sample_rate_ = sample_rate;
    channels_ = channels;
    frame_duration_ms_ = frame_duration_ms;
    frame_samples_ = sample_rate * frame_duration_ms / 1000;

    int error = 0;
    decoder_ = opus_decoder_create(sample_rate, channels, &error);
    if (error != OPUS_OK || decoder_ == nullptr) {
        spdlog::error("Opus解码器创建失败: {}", opus_strerror(error));
        decoder_ = nullptr;
        return false;
    }

    // 分配解码缓冲区
    decode_buffer_float_.resize(frame_samples_ * channels);
    decode_buffer_int16_.resize(frame_samples_ * channels);

    spdlog::info("Opus解码器初始化成功: sample_rate={}, channels={}, frame_ms={}",
                 sample_rate, channels, frame_duration_ms);
    return true;
}

void OpusDecoderWrapper::cleanup() {
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
        spdlog::debug("Opus解码器已释放");
    }
    decode_buffer_float_.clear();
    decode_buffer_int16_.clear();
}

std::vector<float> OpusDecoderWrapper::decode(const uint8_t* data, int len, int samples_per_frame) {
    if (!decoder_) {
        spdlog::error("Opus解码器未初始化");
        return {};
    }
    if (!data || len <= 0) {
        // 可能是丢包，尝试PLC（丢包隐藏）
        if (decode_buffer_float_.size() < static_cast<size_t>(samples_per_frame * channels_)) {
            decode_buffer_float_.resize(samples_per_frame * channels_);
        }
        int decoded = opus_decode_float(decoder_, nullptr, 0,
                                        decode_buffer_float_.data(), samples_per_frame, 0);
        if (decoded < 0) {
            spdlog::error("Opus PLC解码失败: {}", opus_strerror(decoded));
            return {};
        }
        return std::vector<float>(decode_buffer_float_.begin(),
                                  decode_buffer_float_.begin() + decoded * channels_);
    }

    if (decode_buffer_float_.size() < static_cast<size_t>(samples_per_frame * channels_)) {
        decode_buffer_float_.resize(samples_per_frame * channels_);
    }

    int decoded = opus_decode_float(decoder_, data, len,
                                    decode_buffer_float_.data(), samples_per_frame, 0);
    if (decoded < 0) {
        spdlog::error("Opus解码失败: {}", opus_strerror(decoded));
        return {};
    }

    return std::vector<float>(decode_buffer_float_.begin(),
                              decode_buffer_float_.begin() + decoded * channels_);
}

std::vector<int16_t> OpusDecoderWrapper::decode_int16(const uint8_t* data, int len, int samples_per_frame) {
    if (!decoder_) {
        spdlog::error("Opus解码器未初始化");
        return {};
    }
    if (!data || len <= 0) {
        // PLC丢包隐藏
        if (decode_buffer_int16_.size() < static_cast<size_t>(samples_per_frame * channels_)) {
            decode_buffer_int16_.resize(samples_per_frame * channels_);
        }
        int decoded = opus_decode(decoder_, nullptr, 0,
                                  decode_buffer_int16_.data(), samples_per_frame, 0);
        if (decoded < 0) {
            spdlog::error("Opus PLC解码失败: {}", opus_strerror(decoded));
            return {};
        }
        return std::vector<int16_t>(decode_buffer_int16_.begin(),
                                    decode_buffer_int16_.begin() + decoded * channels_);
    }

    if (decode_buffer_int16_.size() < static_cast<size_t>(samples_per_frame * channels_)) {
        decode_buffer_int16_.resize(samples_per_frame * channels_);
    }

    int decoded = opus_decode(decoder_, data, len,
                              decode_buffer_int16_.data(), samples_per_frame, 0);
    if (decoded < 0) {
        spdlog::error("Opus解码失败: {}", opus_strerror(decoded));
        return {};
    }

    return std::vector<int16_t>(decode_buffer_int16_.begin(),
                                decode_buffer_int16_.begin() + decoded * channels_);
}

} // namespace voice::media
