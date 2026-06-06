#pragma once

#include <QObject>
#include <QTimer>
#include <functional>
#include <vector>

enum class ToneType {
    Join,
    Leave,
    Mute,
    Unmute,
    Alarm,
    Custom
};

class ToneService : public QObject {
    Q_OBJECT

public:
    explicit ToneService(QObject* parent = nullptr);
    ~ToneService();

    void play_tone(ToneType type);
    void stop_tone();
    void set_pcm_callback(std::function<void(const float*, int)> callback);

signals:
    void tone_started(ToneType type);
    void tone_finished(ToneType type);

private slots:
    void generate_tone_frame();

private:
    void generate_sine_wave(float* buffer, int frames, float freq, float amplitude);
    void generate_beep(float* buffer, int frames);

    QTimer* timer_;
    ToneType current_tone_;
    int remaining_frames_;
    float phase_;
    std::function<void(const float*, int)> pcm_callback_;
    std::vector<float> frame_buffer_;
    int sample_rate_;
};
