#pragma once

#include <QMainWindow>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QTimer>

#include "dtls_srtp.h"
#include "signaling_client.h"
#include "control_server.h"
#include "audio_manager.h"

class ClientTestWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ClientTestWindow(QWidget* parent = nullptr);
    ~ClientTestWindow();

private slots:
    // 原有
    void onTestDtlsSrtp();
    void onToggleAec(bool checked);
    void onReset();

    // SignalingClient 测试
    void onSignalingConnect();
    void onSignalingDisconnect();
    void onSignalingJoin();
    void onSignalingLeave();
    void onSignalingMute();
    void onSignalingUnmute();
    void onSignalingStatus();

    // ControlServer 测试
    void onControlStart();
    void onControlStop();
    void onControlSendCommand();

    // AudioManager 测试
    void onAudioInit();
    void onAudioCleanup();
    void onAudioStartCapture();
    void onAudioStopCapture();
    void onAudioStartPlayback();
    void onAudioStopPlayback();
    void onAudioToggleMute(bool checked);
    void onAudioToggleSilence(bool checked);
    void onAudioRefresh();

private:
    void setupUI();
    void logMessage(const QString& msg);

    // 日志
    QTextEdit* log_text_edit_;

    // DTLS-SRTP 会话
    std::unique_ptr<DtlsSrtpSession> dtls_session_server_;
    std::unique_ptr<DtlsSrtpSession> dtls_session_client_;

    // AEC 状态
    bool aec_enabled_;

    // --- SignalingClient 测试 ---
    QLineEdit* sig_host_input_;
    QSpinBox*  sig_port_input_;
    QLineEdit* sig_room_input_;
    QLineEdit* sig_user_input_;
    QComboBox* sig_role_combo_;
    QLabel*    sig_status_label_;
    QTimer*    sig_status_timer_;
    voice::client::SignalingClient* signaling_client_ = nullptr;

    // --- ControlServer 测试 ---
    QSpinBox* ctrl_port_input_;
    QLineEdit* ctrl_cmd_input_;
    QLabel*    ctrl_status_label_;
    voice::client::ControlServer control_server_;

    // --- AudioManager 测试 ---
    QCheckBox* audio_mute_check_;
    QCheckBox* audio_silence_check_;
    QLabel*    audio_status_label_;
    QTimer*    audio_status_timer_;
    voice::client::AudioManager audio_mgr_;
};
