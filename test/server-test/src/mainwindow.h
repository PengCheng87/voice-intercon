#pragma once

#include <QMainWindow>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QTimer>

#include "priority_mixer.h"
#include "media_stats.h"
#include "tone_service.h"
#include "room_manager.h"
#include "sfu_manager.h"
#include "signaling_server.h"

class ServerTestWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ServerTestWindow(QWidget* parent = nullptr);
    ~ServerTestWindow();

private slots:
    // 原有
    void onPlayJoinTone();
    void onPlayLeaveTone();
    void onPlayAlarmTone();
    void onTestPriorityMixer();
    void onResetStats();
    void onUpdateStats();

    // RoomManager 测试
    void onCreateRoom();
    void onRemoveRoom();
    void onAddParticipant();
    void onRemoveParticipant();
    void onListRooms();

    // SFUManager 测试
    void onCreateTransport();
    void onAddProducer();
    void onRemoveProducer();
    void onAddConsumer();
    void onListProducers();

    // SignalingServer 测试
    void onGenerateCert();
    void onInitStartSignaling();
    void onStopSignaling();
    void onRefreshSignalingStatus();

private:
    void setupUI();
    void logMessage(const QString& msg);

    // 日志
    QTextEdit* log_text_edit_;

    // 提示音服务
    ToneService* tone_service_;

    // 优先级混音器
    std::unique_ptr<PriorityMixer> priority_mixer_;

    // 媒体统计
    std::unique_ptr<MediaStats> media_stats_;

    // 统计更新定时器
    QTimer* stats_timer_;

    // --- RoomManager 测试 ---
    QLineEdit* room_id_input_;
    QLineEdit* room_user_input_;
    QComboBox* room_role_combo_;

    // --- SFUManager 测试 ---
    QLineEdit* sfu_room_input_;
    QLineEdit* sfu_participant_input_;

    // --- SignalingServer 测试 ---
    QSpinBox* signaling_port_input_;
    QLabel* signaling_status_label_;
    QTimer* signaling_status_timer_;

    // 被测对象实例
    voice::sfu::RoomManager room_mgr_;
    voice::sfu::SFUManager sfu_mgr_;
    std::unique_ptr<voice::sfu::SignalingServer> signaling_server_;
};
