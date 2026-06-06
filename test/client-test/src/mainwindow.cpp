#include "mainwindow.h"
#include <QDateTime>
#include <QCoreApplication>
#include <vector>
#include <random>

// ============================================================
// 构造/析构
// ============================================================

ClientTestWindow::ClientTestWindow(QWidget* parent)
    : QMainWindow(parent)
    , dtls_session_server_(std::make_unique<DtlsSrtpSession>())
    , dtls_session_client_(std::make_unique<DtlsSrtpSession>())
    , aec_enabled_(true)
{
    setupUI();
    logMessage("Client Test Program initialized");
}

ClientTestWindow::~ClientTestWindow() {
    sig_status_timer_->stop();
    audio_status_timer_->stop();

    if (signaling_client_) {
        signaling_client_->disconnect_from_server();
        delete signaling_client_;
        signaling_client_ = nullptr;
    }
    control_server_.stop();
    audio_mgr_.cleanup();
}

// ============================================================
// UI 布局
// ============================================================

void ClientTestWindow::setupUI() {
    setWindowTitle("Voice Intercom - Client Test");
    setMinimumSize(850, 800);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* main_layout = new QVBoxLayout(central);

    // --- 原有: DTLS-SRTP ---
    auto* dtls_grp = new QGroupBox("DTLS-SRTP Test", this);
    auto* dtls_btn = new QPushButton("Test DTLS-SRTP Encryption/Decryption", this);
    connect(dtls_btn, &QPushButton::clicked, this, &ClientTestWindow::onTestDtlsSrtp);
    dtls_grp->setLayout(new QVBoxLayout);
    dtls_grp->layout()->addWidget(dtls_btn);
    main_layout->addWidget(dtls_grp);

    // --- 原有: AEC ---
    auto* aec_grp = new QGroupBox("AEC (Echo Cancellation) Test", this);
    auto* aec_cb = new QCheckBox("Enable AEC", this);
    aec_cb->setChecked(true);
    connect(aec_cb, &QCheckBox::toggled, this, &ClientTestWindow::onToggleAec);
    aec_grp->setLayout(new QVBoxLayout);
    aec_grp->layout()->addWidget(aec_cb);
    main_layout->addWidget(aec_grp);

    // ============================================================
    // 新增: SignalingClient 测试
    // ============================================================
    auto* sig_grp = new QGroupBox("SignalingClient Test", this);
    auto* sig_layout = new QVBoxLayout(sig_grp);

    // 输入行
    auto* sig_row1 = new QHBoxLayout;
    sig_host_input_ = new QLineEdit(this);
    sig_host_input_->setPlaceholderText("Server host");
    sig_host_input_->setText("127.0.0.1");
    sig_port_input_ = new QSpinBox(this);
    sig_port_input_->setRange(1, 65535);
    sig_port_input_->setValue(9090);
    sig_room_input_ = new QLineEdit(this);
    sig_room_input_->setPlaceholderText("Room ID");
    sig_room_input_->setText("test_room");
    sig_user_input_ = new QLineEdit(this);
    sig_user_input_->setPlaceholderText("User ID");
    sig_user_input_->setText("test_user");
    sig_role_combo_ = new QComboBox(this);
    sig_role_combo_->addItems({"doctor", "host"});

    sig_row1->addWidget(new QLabel("Host:", this));
    sig_row1->addWidget(sig_host_input_, 1);
    sig_row1->addWidget(new QLabel("Port:", this));
    sig_row1->addWidget(sig_port_input_);
    sig_row1->addWidget(new QLabel("Room:", this));
    sig_row1->addWidget(sig_room_input_, 1);
    sig_row1->addWidget(new QLabel("User:", this));
    sig_row1->addWidget(sig_user_input_, 1);
    sig_row1->addWidget(new QLabel("Role:", this));
    sig_row1->addWidget(sig_role_combo_);
    sig_layout->addLayout(sig_row1);

    // 按钮行
    auto* sig_btns = new QHBoxLayout;
    auto* btn_conn  = new QPushButton("Connect", this);
    auto* btn_dis   = new QPushButton("Disconnect", this);
    auto* btn_join  = new QPushButton("Send Join", this);
    auto* btn_leave = new QPushButton("Send Leave", this);
    auto* btn_mute  = new QPushButton("Send Mute", this);
    auto* btn_umute = new QPushButton("Send Unmute", this);
    auto* btn_stat  = new QPushButton("Status", this);
    connect(btn_conn,  &QPushButton::clicked, this, &ClientTestWindow::onSignalingConnect);
    connect(btn_dis,   &QPushButton::clicked, this, &ClientTestWindow::onSignalingDisconnect);
    connect(btn_join,  &QPushButton::clicked, this, &ClientTestWindow::onSignalingJoin);
    connect(btn_leave, &QPushButton::clicked, this, &ClientTestWindow::onSignalingLeave);
    connect(btn_mute,  &QPushButton::clicked, this, &ClientTestWindow::onSignalingMute);
    connect(btn_umute, &QPushButton::clicked, this, &ClientTestWindow::onSignalingUnmute);
    connect(btn_stat,  &QPushButton::clicked, this, &ClientTestWindow::onSignalingStatus);
    sig_btns->addWidget(btn_conn);
    sig_btns->addWidget(btn_dis);
    sig_btns->addWidget(btn_join);
    sig_btns->addWidget(btn_leave);
    sig_btns->addWidget(btn_mute);
    sig_btns->addWidget(btn_umute);
    sig_btns->addWidget(btn_stat);
    sig_layout->addLayout(sig_btns);

    // 状态标签
    sig_status_label_ = new QLabel("Status: Not connected", this);
    sig_layout->addWidget(sig_status_label_);
    main_layout->addWidget(sig_grp);

    // ============================================================
    // 新增: ControlServer 测试
    // ============================================================
    auto* ctrl_grp = new QGroupBox("ControlServer Test", this);
    auto* ctrl_layout = new QVBoxLayout(ctrl_grp);

    auto* ctrl_row1 = new QHBoxLayout;
    ctrl_port_input_ = new QSpinBox(this);
    ctrl_port_input_->setRange(1025, 65535);
    ctrl_port_input_->setValue(19090);
    auto* btn_ctrl_start = new QPushButton("Start", this);
    auto* btn_ctrl_stop  = new QPushButton("Stop", this);
    connect(btn_ctrl_start, &QPushButton::clicked, this, &ClientTestWindow::onControlStart);
    connect(btn_ctrl_stop,  &QPushButton::clicked, this, &ClientTestWindow::onControlStop);
    ctrl_row1->addWidget(new QLabel("Port:", this));
    ctrl_row1->addWidget(ctrl_port_input_);
    ctrl_row1->addWidget(btn_ctrl_start);
    ctrl_row1->addWidget(btn_ctrl_stop);

    ctrl_status_label_ = new QLabel("Running: No", this);
    ctrl_row1->addWidget(ctrl_status_label_);
    ctrl_layout->addLayout(ctrl_row1);

    auto* ctrl_row2 = new QHBoxLayout;
    ctrl_cmd_input_ = new QLineEdit(this);
    ctrl_cmd_input_->setPlaceholderText("Command (e.g. status, mute, list)");
    ctrl_cmd_input_->setText("status");
    auto* btn_ctrl_send = new QPushButton("Send Command", this);
    connect(btn_ctrl_send, &QPushButton::clicked, this, &ClientTestWindow::onControlSendCommand);
    ctrl_row2->addWidget(ctrl_cmd_input_, 1);
    ctrl_row2->addWidget(btn_ctrl_send);
    ctrl_layout->addLayout(ctrl_row2);
    main_layout->addWidget(ctrl_grp);

    // ============================================================
    // 新增: AudioManager 测试
    // ============================================================
    auto* audio_grp = new QGroupBox("AudioManager Test", this);
    auto* audio_layout = new QVBoxLayout(audio_grp);

    auto* audio_btns1 = new QHBoxLayout;
    auto* btn_audio_init    = new QPushButton("Init", this);
    auto* btn_audio_cleanup = new QPushButton("Cleanup", this);
    auto* btn_audio_start_c = new QPushButton("Start Capture", this);
    auto* btn_audio_stop_c  = new QPushButton("Stop Capture", this);
    auto* btn_audio_start_p = new QPushButton("Start Playback", this);
    auto* btn_audio_stop_p  = new QPushButton("Stop Playback", this);
    connect(btn_audio_init,    &QPushButton::clicked, this, &ClientTestWindow::onAudioInit);
    connect(btn_audio_cleanup, &QPushButton::clicked, this, &ClientTestWindow::onAudioCleanup);
    connect(btn_audio_start_c, &QPushButton::clicked, this, &ClientTestWindow::onAudioStartCapture);
    connect(btn_audio_stop_c,  &QPushButton::clicked, this, &ClientTestWindow::onAudioStopCapture);
    connect(btn_audio_start_p, &QPushButton::clicked, this, &ClientTestWindow::onAudioStartPlayback);
    connect(btn_audio_stop_p,  &QPushButton::clicked, this, &ClientTestWindow::onAudioStopPlayback);
    audio_btns1->addWidget(btn_audio_init);
    audio_btns1->addWidget(btn_audio_cleanup);
    audio_btns1->addWidget(btn_audio_start_c);
    audio_btns1->addWidget(btn_audio_stop_c);
    audio_btns1->addWidget(btn_audio_start_p);
    audio_btns1->addWidget(btn_audio_stop_p);
    audio_layout->addLayout(audio_btns1);

    auto* audio_btns2 = new QHBoxLayout;
    audio_mute_check_ = new QCheckBox("Mute", this);
    audio_silence_check_ = new QCheckBox("Silence", this);
    connect(audio_mute_check_, &QCheckBox::toggled, this, &ClientTestWindow::onAudioToggleMute);
    connect(audio_silence_check_, &QCheckBox::toggled, this, &ClientTestWindow::onAudioToggleSilence);
    auto* btn_audio_refresh = new QPushButton("Refresh Status", this);
    connect(btn_audio_refresh, &QPushButton::clicked, this, &ClientTestWindow::onAudioRefresh);
    audio_btns2->addWidget(audio_mute_check_);
    audio_btns2->addWidget(audio_silence_check_);
    audio_btns2->addWidget(btn_audio_refresh);
    audio_layout->addLayout(audio_btns2);

    audio_status_label_ = new QLabel("Init: No | Capture: No | Play: No | Mute: No | Level: 0.0", this);
    audio_layout->addWidget(audio_status_label_);
    main_layout->addWidget(audio_grp);

    // --- 原有: Reset ---
    auto* reset_grp = new QGroupBox("Reset", this);
    auto* btn_reset = new QPushButton("Reset All", this);
    connect(btn_reset, &QPushButton::clicked, this, &ClientTestWindow::onReset);
    reset_grp->setLayout(new QVBoxLayout);
    reset_grp->layout()->addWidget(btn_reset);
    main_layout->addWidget(reset_grp);

    // --- 日志 ---
    auto* log_grp = new QGroupBox("Log", this);
    auto* log_layout = new QVBoxLayout(log_grp);
    log_text_edit_ = new QTextEdit(this);
    log_text_edit_->setReadOnly(true);
    log_layout->addWidget(log_text_edit_);
    main_layout->addWidget(log_grp);

    // 定时器
    sig_status_timer_ = new QTimer(this);
    connect(sig_status_timer_, &QTimer::timeout, this, &ClientTestWindow::onSignalingStatus);
    sig_status_timer_->start(3000);

    audio_status_timer_ = new QTimer(this);
    connect(audio_status_timer_, &QTimer::timeout, this, &ClientTestWindow::onAudioRefresh);
    audio_status_timer_->start(2000);
}

// ============================================================
// 日志
// ============================================================

void ClientTestWindow::logMessage(const QString& msg) {
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    log_text_edit_->append(QString("[%1] %2").arg(ts, msg));
}

// ============================================================
// 原有 Slots
// ============================================================

void ClientTestWindow::onTestDtlsSrtp() {
    logMessage("Testing DTLS-SRTP...");

    if (!dtls_session_server_->init(true)) {
        logMessage("Failed to initialize DTLS server session");
        return;
    }
    if (!dtls_session_client_->init(false)) {
        logMessage("Failed to initialize DTLS client session");
        return;
    }
    logMessage("DTLS sessions initialized");

    QString server_fp = QString::fromStdString(dtls_session_server_->get_local_fingerprint());
    QString client_fp = QString::fromStdString(dtls_session_client_->get_local_fingerprint());
    logMessage(QString("Server fingerprint: %1").arg(server_fp));
    logMessage(QString("Client fingerprint: %1").arg(client_fp));

    dtls_session_server_->set_remote_fingerprint(client_fp.toStdString());
    dtls_session_client_->set_remote_fingerprint(server_fp.toStdString());

    std::vector<uint8_t> dummy_response;
    dtls_session_server_->process_dtls_packet(nullptr, 0, dummy_response);
    dtls_session_client_->process_dtls_packet(nullptr, 0, dummy_response);

    if (dtls_session_server_->is_handshake_complete() && dtls_session_client_->is_handshake_complete()) {
        logMessage("DTLS handshake completed");
    } else {
        logMessage("DTLS handshake failed");
        return;
    }

    std::vector<uint8_t> original_data = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08};
    std::vector<uint8_t> encrypted(original_data.size());
    std::vector<uint8_t> decrypted(original_data.size());
    size_t out_len = 0;

    logMessage(QString("Original data: %1 bytes").arg(original_data.size()));

    if (dtls_session_client_->encrypt_rtp(original_data.data(), original_data.size(),
                                           encrypted.data(), &out_len)) {
        logMessage(QString("Encryption successful: %1 bytes").arg(out_len));
        if (dtls_session_server_->decrypt_rtp(encrypted.data(), out_len,
                                               decrypted.data(), &out_len)) {
            logMessage(QString("Decryption successful: %1 bytes").arg(out_len));
            bool match = true;
            for (size_t i = 0; i < original_data.size(); ++i)
                if (original_data[i] != decrypted[i]) { match = false; break; }
            if (match)
                logMessage("Data integrity verified: encryption/decryption works correctly");
            else
                logMessage("Data mismatch: decrypted data does not match original");
        } else {
            logMessage("Decryption failed");
        }
    } else {
        logMessage("Encryption failed");
    }
}

void ClientTestWindow::onToggleAec(bool checked) {
    aec_enabled_ = checked;
    logMessage(QString("AEC %1").arg(checked ? "enabled" : "disabled"));
}

void ClientTestWindow::onReset() {
    logMessage("Resetting test...");

    // 断开信令
    if (signaling_client_) {
        signaling_client_->disconnect_from_server();
        delete signaling_client_;
        signaling_client_ = nullptr;
    }
    sig_status_label_->setText("Status: Not connected");

    // 重置 DTLS-SRTP
    dtls_session_server_->cleanup();
    dtls_session_client_->cleanup();

    aec_enabled_ = true;

    logMessage("Test reset completed");
}

// ============================================================
// SignalingClient 测试
// ============================================================

void ClientTestWindow::onSignalingConnect() {
    if (signaling_client_) {
        logMessage("Already connected, disconnect first");
        return;
    }

    QString host = sig_host_input_->text().trimmed();
    int port = sig_port_input_->value();
    if (host.isEmpty()) { logMessage("Server host is required"); return; }

    // SignalingClient 是 QObject，需要 parent
    auto* client = new voice::client::SignalingClient(this);

    // 连接信号
    connect(client, &voice::client::SignalingClient::connected, this, [this]() {
        logMessage("SIGNAL: connected (DTLS handshake complete)");
        sig_status_label_->setText("Status: Connected");
    });
    connect(client, &voice::client::SignalingClient::disconnected, this, [this](const QString& reason) {
        logMessage(QString("SIGNAL: disconnected, reason=%1").arg(reason));
        sig_status_label_->setText("Status: Disconnected");
    });
    connect(client, &voice::client::SignalingClient::joinAccepted, this,
            [this](const std::vector<voice::client::Participant>& participants) {
        logMessage(QString("SIGNAL: join accepted, participants=%1").arg(participants.size()));
        sig_status_label_->setText("Status: Joined");
    });
    connect(client, &voice::client::SignalingClient::participantUpdate, this,
            [this](const voice::client::Participant& p, bool joined) {
        logMessage(QString("SIGNAL: participant %1 %2")
                       .arg(QString::fromStdString(p.user_id))
                       .arg(joined ? "joined" : "left"));
    });
    connect(client, &voice::client::SignalingClient::audioToggle, this,
            [this](const QString& uid, bool muted) {
        logMessage(QString("SIGNAL: audioToggle user=%1 muted=%2").arg(uid).arg(muted));
    });
    connect(client, &voice::client::SignalingClient::tonePlayed, this,
            [this](const QString& tid, const QString& uid) {
        logMessage(QString("SIGNAL: tonePlayed id=%1 user=%2").arg(tid).arg(uid));
    });

    bool ok = client->connect_to_server(host, static_cast<quint16>(port), false);
    if (ok) {
        signaling_client_ = client;
        logMessage(QString("Connecting to %1:%2 ...").arg(host).arg(port));
        sig_status_label_->setText("Status: Connecting...");
    } else {
        logMessage(QString("Connect failed: %1").arg(QString::fromStdString(client->get_last_error())));
        delete client;
    }
}

void ClientTestWindow::onSignalingDisconnect() {
    if (!signaling_client_) {
        logMessage("Not connected");
        return;
    }
    signaling_client_->disconnect_from_server();
    delete signaling_client_;
    signaling_client_ = nullptr;
    sig_status_label_->setText("Status: Disconnected");
    logMessage("Disconnected");
}

void ClientTestWindow::onSignalingJoin() {
    if (!signaling_client_ || !signaling_client_->is_connected()) {
        logMessage("Not connected, connect first");
        return;
    }
    QString room = sig_room_input_->text().trimmed();
    QString user = sig_user_input_->text().trimmed();
    QString role = sig_role_combo_->currentText();
    if (room.isEmpty() || user.isEmpty()) {
        logMessage("Room ID and User ID are required");
        return;
    }
    bool ok = signaling_client_->send_join(room.toStdString(), user.toStdString(), role.toStdString());
    logMessage(QString("Send Join: %1").arg(ok ? "OK" : "Failed"));
}

void ClientTestWindow::onSignalingLeave() {
    if (!signaling_client_) { logMessage("Not connected"); return; }
    bool ok = signaling_client_->send_leave();
    logMessage(QString("Send Leave: %1").arg(ok ? "OK" : "Failed"));
}

void ClientTestWindow::onSignalingMute() {
    if (!signaling_client_) { logMessage("Not connected"); return; }
    bool ok = signaling_client_->send_mute(true);
    logMessage(QString("Send Mute: %1").arg(ok ? "OK" : "Failed"));
}

void ClientTestWindow::onSignalingUnmute() {
    if (!signaling_client_) { logMessage("Not connected"); return; }
    bool ok = signaling_client_->send_mute(false);
    logMessage(QString("Send Unmute: %1").arg(ok ? "OK" : "Failed"));
}

void ClientTestWindow::onSignalingStatus() {
    if (!signaling_client_) {
        sig_status_label_->setText("Status: Not connected");
        return;
    }
    bool conn = signaling_client_->is_connected();
    auto parts = signaling_client_->get_participants();
    sig_status_label_->setText(
        QString("Connected: %1 | Participants: %2")
            .arg(conn ? "Yes" : "No")
            .arg(parts.size()));
}

// ============================================================
// ControlServer 测试
// ============================================================

void ClientTestWindow::onControlStart() {
    if (control_server_.is_running()) {
        logMessage("ControlServer already running");
        return;
    }

    // 设置命令处理器（简单回声 + 基础命令）
    control_server_.set_command_handler([this](const std::string& cmd, const std::string& params) {
        nlohmann::json resp;
        resp["success"] = true;
        if (cmd == "status") {
            resp["data"]["connected"] = signaling_client_ ? signaling_client_->is_connected() : false;
            resp["data"]["audio_mgr_ready"] = audio_mgr_.is_initialized();
        } else if (cmd == "mute") {
            audio_mgr_.set_mute(true);
            resp["message"] = "muted";
        } else if (cmd == "unmute") {
            audio_mgr_.set_mute(false);
            resp["message"] = "unmuted";
        } else {
            resp["echo"]["command"] = cmd;
            resp["echo"]["params"]  = params;
        }
        return resp.dump();
    });

    int port = ctrl_port_input_->value();
    if (control_server_.start(port)) {
        logMessage(QString("ControlServer started on port %1").arg(port));
        ctrl_status_label_->setText("Running: Yes");
    } else {
        logMessage(QString("ControlServer start failed: %1")
                       .arg(QString::fromStdString(control_server_.get_last_error())));
    }
}

void ClientTestWindow::onControlStop() {
    control_server_.stop();
    ctrl_status_label_->setText("Running: No");
    logMessage("ControlServer stopped");
}

void ClientTestWindow::onControlSendCommand() {
    if (!control_server_.is_running()) {
        logMessage("ControlServer is not running, start it first");
        return;
    }
    int port = ctrl_port_input_->value();
    QString cmd_text = ctrl_cmd_input_->text().trimmed();
    if (cmd_text.isEmpty()) { logMessage("Command is empty"); return; }

    QString response = QString::fromStdString(
        voice::client::ControlServer::send_command(port, cmd_text.toStdString()));
    logMessage(QString("Command: %1").arg(cmd_text));
    logMessage(QString("Response: %1").arg(response));
}

// ============================================================
// AudioManager 测试
// ============================================================

void ClientTestWindow::onAudioInit() {
    if (audio_mgr_.is_initialized()) {
        logMessage("AudioManager already initialized");
        return;
    }
    bool ok = audio_mgr_.init("default", "default", 48000, 1);
    logMessage(QString("AudioManager init: %1").arg(ok ? "OK" : "Failed"));
    onAudioRefresh();
}

void ClientTestWindow::onAudioCleanup() {
    audio_mgr_.cleanup();
    logMessage("AudioManager cleaned up");
    audio_mute_check_->setChecked(false);
    audio_silence_check_->setChecked(false);
    onAudioRefresh();
}

void ClientTestWindow::onAudioStartCapture() {
    if (!audio_mgr_.is_initialized()) { logMessage("Init AudioManager first"); return; }
    bool ok = audio_mgr_.start_capture();
    logMessage(QString("Start capture: %1").arg(ok ? "OK" : "Failed"));
    onAudioRefresh();
}

void ClientTestWindow::onAudioStopCapture() {
    bool ok = audio_mgr_.stop_capture();
    logMessage(QString("Stop capture: %1").arg(ok ? "OK" : "Failed"));
    onAudioRefresh();
}

void ClientTestWindow::onAudioStartPlayback() {
    if (!audio_mgr_.is_initialized()) { logMessage("Init AudioManager first"); return; }
    bool ok = audio_mgr_.start_playback();
    logMessage(QString("Start playback: %1").arg(ok ? "OK" : "Failed"));
    onAudioRefresh();
}

void ClientTestWindow::onAudioStopPlayback() {
    bool ok = audio_mgr_.stop_playback();
    logMessage(QString("Stop playback: %1").arg(ok ? "OK" : "Failed"));
    onAudioRefresh();
}

void ClientTestWindow::onAudioToggleMute(bool checked) {
    audio_mgr_.set_mute(checked);
    logMessage(checked ? "Mute enabled" : "Mute disabled");
    onAudioRefresh();
}

void ClientTestWindow::onAudioToggleSilence(bool checked) {
    audio_mgr_.set_silence(checked);
    logMessage(checked ? "Silence enabled" : "Silence disabled");
    onAudioRefresh();
}

void ClientTestWindow::onAudioRefresh() {
    audio_status_label_->setText(
        QString("Init: %1 | Capture: %2 | Play: %3 | Mute: %4 | Level: %5")
            .arg(audio_mgr_.is_initialized() ? "Yes" : "No")
            .arg(audio_mgr_.is_capturing() ? "Yes" : "No")
            .arg(audio_mgr_.is_playing() ? "Yes" : "No")
            .arg(audio_mgr_.is_muted() ? "Yes" : "No")
            .arg(audio_mgr_.get_audio_level(), 0, 'f', 2));
}
