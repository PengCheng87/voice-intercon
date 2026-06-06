#include "tone_service.h"
#include <cmath>
#include <spdlog/spdlog.h>

ToneService::ToneService(QObject* parent)
    : QObject(parent)
    , timer_(new QTimer(this))
    , current_tone_(ToneType::Custom)
    , remaining_frames_(0)
    , phase_(0.0f)
    , sample_rate_(48000)
{
    frame_buffer_.resize(960); // 20ms @ 48kHz
    connect(timer_, &QTimer::timeout, this, &ToneService::generate_tone_frame);
}

ToneService::~ToneService() {
    stop_tone();
}

void ToneService::play_tone(ToneType type) {
    current_tone_ = type;
    remaining_frames_ = 48; // ~1 second (48 * 20ms = 960ms)
    phase_ = 0.0f;

    emit tone_started(type);
    timer_->start(20);
}

void ToneService::stop_tone() {
    timer_->stop();
    if (remaining_frames_ > 0) {
        emit tone_finished(current_tone_);
    }
    remaining_frames_ = 0;
}

void ToneService::set_pcm_callback(std::function<void(const float*, int)> callback) {
    pcm_callback_ = std::move(callback);
}

void ToneService::generate_tone_frame() {
    if (remaining_frames_ <= 0) {
        stop_tone();
        return;
    }

    switch (current_tone_) {
        case ToneType::Join:
            generate_sine_wave(frame_buffer_.data(), 960, 880.0f, 0.3f);
            break;
        case ToneType::Leave:
            generate_sine_wave(frame_buffer_.data(), 960, 440.0f, 0.3f);
            break;
        case ToneType::Mute:
            generate_sine_wave(frame_buffer_.data(), 960, 220.0f, 0.2f);
            break;
        case ToneType::Unmute:
            generate_sine_wave(frame_buffer_.data(), 960, 660.0f, 0.3f);
            break;
        case ToneType::Alarm:
            generate_beep(frame_buffer_.data(), 960);
            break;
        case ToneType::Custom:
            generate_sine_wave(frame_buffer_.data(), 960, 1000.0f, 0.3f);
            break;
    }

    if (pcm_callback_) {
        pcm_callback_(frame_buffer_.data(), 960);
    }

    remaining_frames_--;
}

void ToneService::generate_sine_wave(float* buffer, int frames, float freq, float amplitude) {
    const float two_pi = 2.0f * 3.14159265359f;
    for (int i = 0; i < frames; ++i) {
        buffer[i] = amplitude * std::sin(phase_);
        phase_ += two_pi * freq / sample_rate_;
        if (phase_ > two_pi) {
            phase_ -= two_pi;
        }
    }
}

void ToneService::generate_beep(float* buffer, int frames) {
    const float two_pi = 2.0f * 3.14159265359f;
    const float freq = 1200.0f;
    const float amplitude = 0.5f;
    
    for (int i = 0; i < frames; ++i) {
        float t = static_cast<float>(i) / sample_rate_;
        float envelope = std::sin(t * 10.0f * two_pi) * 0.5f + 0.5f;
        buffer[i] = amplitude * envelope * std::sin(phase_);
        phase_ += two_pi * freq / sample_rate_;
        if (phase_ > two_pi) {
            phase_ -= two_pi;
        }
    }
}
