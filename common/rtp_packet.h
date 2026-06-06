#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace voice::media {

/// RTP标准包头结构体（12字节）
/// 参考RFC 3550定义
#pragma pack(push, 1)
struct RtpHeader {
    // 第一个字节: version(2bit) + padding(1bit) + extension(1bit) + csrc_count(4bit)
    uint8_t  first_byte;
    // 第二个字节: marker(1bit) + payload_type(7bit)
    uint8_t  second_byte;
    // 序列号（16位，网络字节序）
    uint16_t sequence_number;
    // 时间戳（32位，网络字节序）
    uint32_t timestamp;
    // SSRC（32位，网络字节序）
    uint32_t ssrc;

    // 便捷访问方法
    uint8_t version() const { return (first_byte >> 6) & 0x03; }
    uint8_t padding() const { return (first_byte >> 5) & 0x01; }
    uint8_t extension() const { return (first_byte >> 4) & 0x01; }
    uint8_t csrc_count() const { return first_byte & 0x0F; }
    uint8_t marker() const { return (second_byte >> 7) & 0x01; }
    uint8_t payload_type() const { return second_byte & 0x7F; }

    void set_version(uint8_t v) {
        first_byte = (first_byte & 0x3F) | ((v & 0x03) << 6);
    }
    void set_padding(uint8_t p) {
        first_byte = (first_byte & 0xDF) | ((p & 0x01) << 5);
    }
    void set_extension(uint8_t e) {
        first_byte = (first_byte & 0xEF) | ((e & 0x01) << 4);
    }
    void set_csrc_count(uint8_t c) {
        first_byte = (first_byte & 0xF0) | (c & 0x0F);
    }
    void set_marker(uint8_t m) {
        second_byte = (second_byte & 0x7F) | ((m & 0x01) << 7);
    }
    void set_payload_type(uint8_t pt) {
        second_byte = (second_byte & 0x80) | (pt & 0x7F);
    }

    /// 将序列号转换为网络字节序
    void set_sequence_number(uint16_t seq) {
        sequence_number = htons(seq);
    }
    /// 获取主机字节序的序列号
    uint16_t get_sequence_number() const {
        return ntohs(sequence_number);
    }

    /// 将时间戳转换为网络字节序
    void set_timestamp(uint32_t ts) {
        timestamp = htonl(ts);
    }
    /// 获取主机字节序的时间戳
    uint32_t get_timestamp() const {
        return ntohl(timestamp);
    }

    /// 将SSRC转换为网络字节序
    void set_ssrc(uint32_t src) {
        ssrc = htonl(src);
    }
    /// 获取主机字节序的SSRC
    uint32_t get_ssrc() const {
        return ntohl(ssrc);
    }

    /// 初始化默认RTP头
    void init_defaults(uint8_t pt, uint16_t seq, uint32_t ts, uint32_t src) {
        first_byte = 0x80;  // version=2, padding=0, extension=0, csrc_count=0
        second_byte = pt & 0x7F;
        set_sequence_number(seq);
        set_timestamp(ts);
        set_ssrc(src);
    }
};
#pragma pack(pop)

static_assert(sizeof(RtpHeader) == 12, "RtpHeader必须是12字节");

/// RTP包封装类
class RtpPacket {
public:
    RtpPacket() = default;
    ~RtpPacket() = default;

    /// 从原始数据解析RTP包
    /// @param data 原始数据指针
    /// @param len 数据长度
    /// @return 解析成功返回true
    bool parse(const uint8_t* data, size_t len) {
        if (len < sizeof(RtpHeader)) {
            return false;
        }
        std::memcpy(&header_, data, sizeof(RtpHeader));
        payload_.assign(data + sizeof(RtpHeader), data + len);
        return true;
    }

    /// 构建RTP包为字节流
    /// @return 完整的RTP包数据
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> packet;
        packet.reserve(sizeof(RtpHeader) + payload_.size());
        packet.insert(packet.end(),
                      reinterpret_cast<const uint8_t*>(&header_),
                      reinterpret_cast<const uint8_t*>(&header_) + sizeof(RtpHeader));
        packet.insert(packet.end(), payload_.begin(), payload_.end());
        return packet;
    }

    /// 获取RTP头（只读）
    const RtpHeader& header() const { return header_; }
    /// 获取RTP头（可修改）
    RtpHeader& header() { return header_; }

    /// 获取负载数据（只读）
    const std::vector<uint8_t>& payload() const { return payload_; }
    /// 获取负载数据（可修改）
    std::vector<uint8_t>& payload() { return payload_; }

    /// 设置负载数据
    void set_payload(const uint8_t* data, size_t len) {
        payload_.assign(data, data + len);
    }
    void set_payload(const std::vector<uint8_t>& data) {
        payload_ = data;
    }

    /// 获取完整包大小
    size_t size() const {
        return sizeof(RtpHeader) + payload_.size();
    }

private:
    RtpHeader header_;
    std::vector<uint8_t> payload_;
};

/// RTP序列号生成器
class RtpSequenceGenerator {
public:
    explicit RtpSequenceGenerator(uint16_t initial = 0)
        : current_(initial) {}

    /// 获取下一个序列号
    uint16_t next() {
        return current_++;
    }

    /// 获取当前序列号（不递增）
    uint16_t current() const {
        return current_;
    }

    /// 重置序列号
    void reset(uint16_t value = 0) {
        current_ = value;
    }

private:
    uint16_t current_;
};

/// RTP时间戳计算器
/// 基于采样率计算每帧的时间戳增量
class RtpTimestampGenerator {
public:
    /// @param sample_rate 采样率（如48000）
    /// @param frame_duration_ms 每帧时长毫秒（如20）
    explicit RtpTimestampGenerator(uint32_t sample_rate = 48000,
                                   uint32_t frame_duration_ms = 20)
        : sample_rate_(sample_rate)
        , frame_duration_ms_(frame_duration_ms)
        , current_timestamp_(0) {
        // 每帧的时间戳增量 = sample_rate * frame_duration_ms / 1000
        increment_per_frame_ = sample_rate * frame_duration_ms / 1000;
    }

    /// 获取当前时间戳
    uint32_t current() const {
        return current_timestamp_;
    }

    /// 获取下一帧的时间戳（自动递增）
    uint32_t next() {
        uint32_t ts = current_timestamp_;
        current_timestamp_ += increment_per_frame_;
        return ts;
    }

    /// 重置时间戳
    void reset(uint32_t value = 0) {
        current_timestamp_ = value;
    }

    /// 获取每帧的时间戳增量
    uint32_t increment_per_frame() const {
        return increment_per_frame_;
    }

private:
    uint32_t sample_rate_;
    uint32_t frame_duration_ms_;
    uint32_t increment_per_frame_;
    uint32_t current_timestamp_;
};

} // namespace voice::media
