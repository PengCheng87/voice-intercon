#include "mainwindow.h"
#include <QTimer>
#include <QDateTime>
#include <QRandomGenerator>
#include <QFile>
#include <cmath>

// ============================================================
// 构造/析构
// ============================================================

ServerTestWindow::ServerTestWindow(QWidget* parent)
    : QMainWindow(parent)
    , tone_service_(new ToneService(this))
    , priority_mixer_(std::make_unique<PriorityMixer>())
    , media_stats_(std::make_unique<MediaStats>())
    , stats_timer_(new QTimer(this))
    , signaling_status_timer_(new QTimer(this))
{
    setupUI();

    // 提示音回调
    tone_service_->set_pcm_callback([this](const float* pcm, int frames) {
        (void)pcm;
        logMessage(QString("Tone data generated: %1 frames").arg(frames));
    });

    // 统计定时器
    connect(stats_timer_, &QTimer::timeout, this, &ServerTestWindow::onUpdateStats);
    stats_timer_->start(1000);

    // SignalingServer 状态刷新定时器
    connect(signaling_status_timer_, &QTimer::timeout,
            this, &ServerTestWindow::onRefreshSignalingStatus);

    logMessage("Server Test Program initialized");
}

ServerTestWindow::~ServerTestWindow() {
    if (stats_timer_->isActive()) stats_timer_->stop();
    signaling_status_timer_->stop();
    if (signaling_server_) {
        signaling_server_->stop();
        signaling_server_.reset();
    }
}

// ============================================================
// UI 布局
// ============================================================

void ServerTestWindow::setupUI() {
    setWindowTitle("Voice Intercom - Server Test");
    setMinimumSize(850, 750);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* main_layout = new QVBoxLayout(central);

    // --- 原有: 提示音测试 ---
    auto* tone_grp = new QGroupBox("Tone Service Test", this);
    auto* tone_btn_row = new QHBoxLayout;
    auto* btn_join  = new QPushButton("Play Join Tone", this);
    auto* btn_leave = new QPushButton("Play Leave Tone", this);
    auto* btn_alarm = new QPushButton("Play Alarm Tone", this);
    connect(btn_join,  &QPushButton::clicked, this, &ServerTestWindow::onPlayJoinTone);
    connect(btn_leave, &QPushButton::clicked, this, &ServerTestWindow::onPlayLeaveTone);
    connect(btn_alarm, &QPushButton::clicked, this, &ServerTestWindow::onPlayAlarmTone);
    tone_btn_row->addWidget(btn_join);
    tone_btn_row->addWidget(btn_leave);
    tone_btn_row->addWidget(btn_alarm);
    tone_grp->setLayout(new QVBoxLayout);
    tone_grp->layout()->addItem(tone_btn_row);
    main_layout->addWidget(tone_grp);

    // --- 原有: 混音器测试 ---
    auto* mixer_grp = new QGroupBox("Priority Mixer Test", this);
    auto* btn_mixer = new QPushButton("Test Priority Mixer", this);
    connect(btn_mixer, &QPushButton::clicked, this, &ServerTestWindow::onTestPriorityMixer);
    mixer_grp->setLayout(new QVBoxLayout);
    mixer_grp->layout()->addWidget(btn_mixer);
    main_layout->addWidget(mixer_grp);

    // --- 原有: 统计测试 ---
    auto* stats_grp = new QGroupBox("Media Statistics", this);
    auto* btn_reset = new QPushButton("Reset Statistics", this);
    connect(btn_reset, &QPushButton::clicked, this, &ServerTestWindow::onResetStats);
    stats_grp->setLayout(new QVBoxLayout);
    stats_grp->layout()->addWidget(btn_reset);
    main_layout->addWidget(stats_grp);

    // ============================================================
    // 新增: RoomManager 测试
    // ============================================================
    auto* room_grp = new QGroupBox("RoomManager Test", this);
    auto* room_layout = new QVBoxLayout(room_grp);

    auto* room_row1 = new QHBoxLayout;
    room_id_input_ = new QLineEdit(this);
    room_id_input_->setPlaceholderText("Room ID");
    room_id_input_->setText("room_test");
    room_user_input_ = new QLineEdit(this);
    room_user_input_->setPlaceholderText("User ID");
    room_user_input_->setText("user_a");
    room_role_combo_ = new QComboBox(this);
    room_role_combo_->addItems({"doctor", "host"});
    room_row1->addWidget(new QLabel("Room ID:", this));
    room_row1->addWidget(room_id_input_);
    room_row1->addWidget(new QLabel("User ID:", this));
    room_row1->addWidget(room_user_input_);
    room_row1->addWidget(new QLabel("Role:", this));
    room_row1->addWidget(room_role_combo_);
    room_layout->addLayout(room_row1);

    auto* room_btns = new QHBoxLayout;
    auto* btn_cr_room  = new QPushButton("Create Room", this);
    auto* btn_rm_room  = new QPushButton("Remove Room", this);
    auto* btn_add_part = new QPushButton("Add Participant", this);
    auto* btn_rm_part  = new QPushButton("Remove Participant", this);
    auto* btn_list_rm  = new QPushButton("List All Rooms", this);
    connect(btn_cr_room,  &QPushButton::clicked, this, &ServerTestWindow::onCreateRoom);
    connect(btn_rm_room,  &QPushButton::clicked, this, &ServerTestWindow::onRemoveRoom);
    connect(btn_add_part, &QPushButton::clicked, this, &ServerTestWindow::onAddParticipant);
    connect(btn_rm_part,  &QPushButton::clicked, this, &ServerTestWindow::onRemoveParticipant);
    connect(btn_list_rm,  &QPushButton::clicked, this, &ServerTestWindow::onListRooms);
    room_btns->addWidget(btn_cr_room);
    room_btns->addWidget(btn_rm_room);
    room_btns->addWidget(btn_add_part);
    room_btns->addWidget(btn_rm_part);
    room_btns->addWidget(btn_list_rm);
    room_layout->addLayout(room_btns);
    main_layout->addWidget(room_grp);

    // ============================================================
    // 新增: SFUManager 测试
    // ============================================================
    auto* sfu_grp = new QGroupBox("SFUManager Test", this);
    auto* sfu_layout = new QVBoxLayout(sfu_grp);

    auto* sfu_row1 = new QHBoxLayout;
    sfu_room_input_ = new QLineEdit(this);
    sfu_room_input_->setPlaceholderText("Room ID");
    sfu_room_input_->setText("room_test");
    sfu_participant_input_ = new QLineEdit(this);
    sfu_participant_input_->setPlaceholderText("Participant ID");
    sfu_participant_input_->setText("user_a");
    sfu_row1->addWidget(new QLabel("Room ID:", this));
    sfu_row1->addWidget(sfu_room_input_);
    sfu_row1->addWidget(new QLabel("Participant ID:", this));
    sfu_row1->addWidget(sfu_participant_input_);
    sfu_layout->addLayout(sfu_row1);

    auto* sfu_btns = new QHBoxLayout;
    auto* btn_tr  = new QPushButton("Create Transport", this);
    auto* btn_pr  = new QPushButton("Add Producer", this);
    auto* btn_rpr = new QPushButton("Remove Producer", this);
    auto* btn_cn  = new QPushButton("Add Consumer", this);
    auto* btn_lp  = new QPushButton("List Producers", this);
    connect(btn_tr,  &QPushButton::clicked, this, &ServerTestWindow::onCreateTransport);
    connect(btn_pr,  &QPushButton::clicked, this, &ServerTestWindow::onAddProducer);
    connect(btn_rpr, &QPushButton::clicked, this, &ServerTestWindow::onRemoveProducer);
    connect(btn_cn,  &QPushButton::clicked, this, &ServerTestWindow::onAddConsumer);
    connect(btn_lp,  &QPushButton::clicked, this, &ServerTestWindow::onListProducers);
    sfu_btns->addWidget(btn_tr);
    sfu_btns->addWidget(btn_pr);
    sfu_btns->addWidget(btn_rpr);
    sfu_btns->addWidget(btn_cn);
    sfu_btns->addWidget(btn_lp);
    sfu_layout->addLayout(sfu_btns);
    main_layout->addWidget(sfu_grp);

    // ============================================================
    // 新增: SignalingServer 测试
    // ============================================================
    auto* sig_grp = new QGroupBox("SignalingServer Test", this);
    auto* sig_layout = new QVBoxLayout(sig_grp);

    auto* sig_row1 = new QHBoxLayout;
    signaling_port_input_ = new QSpinBox(this);
    signaling_port_input_->setRange(1025, 65535);
    signaling_port_input_->setValue(9090);
    sig_row1->addWidget(new QLabel("Port:", this));
    sig_row1->addWidget(signaling_port_input_);

    auto* btn_gen_cert = new QPushButton("Generate Cert", this);
    auto* btn_sig_start = new QPushButton("Init & Start", this);
    auto* btn_sig_stop = new QPushButton("Stop", this);
    connect(btn_gen_cert,  &QPushButton::clicked, this, &ServerTestWindow::onGenerateCert);
    connect(btn_sig_start, &QPushButton::clicked, this, &ServerTestWindow::onInitStartSignaling);
    connect(btn_sig_stop,  &QPushButton::clicked, this, &ServerTestWindow::onStopSignaling);
    sig_row1->addWidget(btn_gen_cert);
    sig_row1->addWidget(btn_sig_start);
    sig_row1->addWidget(btn_sig_stop);

    signaling_status_label_ = new QLabel("Status: Not running", this);
    sig_row1->addWidget(signaling_status_label_);
    sig_layout->addLayout(sig_row1);
    main_layout->addWidget(sig_grp);

    // --- 日志 ---
    auto* log_grp = new QGroupBox("Log", this);
    auto* log_layout = new QVBoxLayout(log_grp);
    log_text_edit_ = new QTextEdit(this);
    log_text_edit_->setReadOnly(true);
    log_layout->addWidget(log_text_edit_);
    main_layout->addWidget(log_grp);
}

// ============================================================
// 日志
// ============================================================

void ServerTestWindow::logMessage(const QString& msg) {
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    log_text_edit_->append(QString("[%1] %2").arg(ts, msg));
}

// ============================================================
// 原有 Slots
// ============================================================

void ServerTestWindow::onPlayJoinTone() {
    logMessage("Playing Join Tone");
    tone_service_->play_tone(ToneType::Join);
}

void ServerTestWindow::onPlayLeaveTone() {
    logMessage("Playing Leave Tone");
    tone_service_->play_tone(ToneType::Leave);
}

void ServerTestWindow::onPlayAlarmTone() {
    logMessage("Playing Alarm Tone");
    tone_service_->play_tone(ToneType::Alarm);
}

void ServerTestWindow::onTestPriorityMixer() {
    logMessage("Testing Priority Mixer");

    std::vector<float> test_data(960);
    for (int i = 0; i < 960; ++i)
        test_data[i] = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 48000.0f);

    priority_mixer_->add_stream(0, Priority::P0_Alarm);
    priority_mixer_->add_stream(1, Priority::P1_Voice);
    priority_mixer_->add_stream(2, Priority::P2_Prompt);

    priority_mixer_->write_stream(0, test_data.data(), 960);
    priority_mixer_->write_stream(1, test_data.data(), 960);
    priority_mixer_->write_stream(2, test_data.data(), 960);

    std::vector<float> output(960);
    priority_mixer_->mix(output.data(), 960);

    logMessage("Priority Mixer test completed");
}

void ServerTestWindow::onResetStats() {
    logMessage("Resetting statistics");
    media_stats_->reset();
}

void ServerTestWindow::onUpdateStats() {
    static uint32_t seq = 0;
    static uint64_t ts = 0;

    media_stats_->on_packet_received(seq++, ts++, 100 + (seq % 50));
    if (seq % 20 == 0) seq++;

    auto s = media_stats_->get_stats();
    logMessage(QString("Stats - Packets: %1, Lost: %2, Loss Rate: %3%, Latency: %4ms, Jitter: %5ms, MOS: %6")
                   .arg(s.total_packets).arg(s.lost_packets)
                   .arg(s.packet_loss_rate * 100, 0, 'f', 2)
                   .arg(s.avg_latency_ms, 0, 'f', 2)
                   .arg(s.jitter_ms, 0, 'f', 2)
                   .arg(s.mos_score, 0, 'f', 2));
}

// ============================================================
// RoomManager 测试
// ============================================================

void ServerTestWindow::onCreateRoom() {
    QString rid = room_id_input_->text().trimmed();
    if (rid.isEmpty()) { logMessage("Room ID is required"); return; }
    bool ok = room_mgr_.create_room(rid.toStdString());
    logMessage(QString("Create Room '%1': %2").arg(rid, ok ? "OK" : "Failed (already exists)"));
    onListRooms();
}

void ServerTestWindow::onRemoveRoom() {
    QString rid = room_id_input_->text().trimmed();
    if (rid.isEmpty()) { logMessage("Room ID is required"); return; }
    bool ok = room_mgr_.remove_room(rid.toStdString());
    logMessage(QString("Remove Room '%1': %2").arg(rid, ok ? "OK" : "Not found"));
    onListRooms();
}

void ServerTestWindow::onAddParticipant() {
    QString rid  = room_id_input_->text().trimmed();
    QString uid  = room_user_input_->text().trimmed();
    if (rid.isEmpty() || uid.isEmpty()) {
        logMessage("Room ID and User ID are required");
        return;
    }
    voice::sfu::Participant p;
    p.user_id       = uid.toStdString();
    p.role          = (room_role_combo_->currentIndex() == 0)
                          ? voice::sfu::ParticipantRole::Doctor
                          : voice::sfu::ParticipantRole::Host;
    p.audio_muted   = false;
    p.audio_enabled = true;
    p.connected_at  = std::chrono::system_clock::now();
    p.ws_handle     = nullptr;

    bool ok = room_mgr_.add_participant(rid.toStdString(), p);
    logMessage(QString("Add Participant '%1' to Room '%2': %3").arg(uid, rid, ok ? "OK" : "Failed"));
    onListRooms();
}

void ServerTestWindow::onRemoveParticipant() {
    QString rid = room_id_input_->text().trimmed();
    QString uid = room_user_input_->text().trimmed();
    if (rid.isEmpty() || uid.isEmpty()) {
        logMessage("Room ID and User ID are required");
        return;
    }
    bool ok = room_mgr_.remove_participant(rid.toStdString(), uid.toStdString());
    logMessage(QString("Remove Participant '%1' from Room '%2': %3").arg(uid, rid, ok ? "OK" : "Not found"));
    onListRooms();
}

void ServerTestWindow::onListRooms() {
    size_t cnt = room_mgr_.room_count();
    logMessage(QString("--- Room count: %1 ---").arg(cnt));
}

// ============================================================
// SFUManager 测试
// ============================================================

void ServerTestWindow::onCreateTransport() {
    QString rid = sfu_room_input_->text().trimmed();
    QString pid = sfu_participant_input_->text().trimmed();
    if (rid.isEmpty() || pid.isEmpty()) {
        logMessage("Room ID and Participant ID are required");
        return;
    }
    auto info = sfu_mgr_.create_transport(rid.toStdString(), pid.toStdString());
    logMessage(QString("Transport created: id=%1, port=%2")
                   .arg(QString::fromStdString(info.transport_id)).arg(info.port));
}

void ServerTestWindow::onAddProducer() {
    QString rid = sfu_room_input_->text().trimmed();
    QString pid = sfu_participant_input_->text().trimmed();
    if (rid.isEmpty() || pid.isEmpty()) {
        logMessage("Room ID and Participant ID are required");
        return;
    }
    auto prod_id = sfu_mgr_.add_producer(rid.toStdString(), pid.toStdString(),
                                          voice::sfu::MediaType::Audio);
    logMessage(QString("Producer added: id=%1").arg(QString::fromStdString(prod_id)));
}

void ServerTestWindow::onRemoveProducer() {
    QString rid = sfu_room_input_->text().trimmed();
    QString pid = sfu_participant_input_->text().trimmed();
    if (rid.isEmpty() || pid.isEmpty()) {
        logMessage("Room ID and Participant ID are required");
        return;
    }
    sfu_mgr_.remove_producer(rid.toStdString(), pid.toStdString());
    logMessage(QString("Producer removed for participant '%1' in room '%2'").arg(pid, rid));
}

void ServerTestWindow::onAddConsumer() {
    QString rid = sfu_room_input_->text().trimmed();
    QString pid = sfu_participant_input_->text().trimmed();
    if (rid.isEmpty() || pid.isEmpty()) {
        logMessage("Room ID and Participant ID are required");
        return;
    }
    // 使用第一个可用生产者作为订阅目标
    auto producers = sfu_mgr_.get_producers_json(rid.toStdString());
    std::string target_producer;
    if (producers.is_array() && !producers.empty()) {
        target_producer = producers[0]["producer_id"].get<std::string>();
    } else {
        // 如果没有生产者，先创建一个
        target_producer = sfu_mgr_.add_producer(rid.toStdString(), pid.toStdString(),
                                                 voice::sfu::MediaType::Audio);
        logMessage(QString("Auto-created producer: %1").arg(QString::fromStdString(target_producer)));
    }
    auto cid = sfu_mgr_.add_consumer(rid.toStdString(), pid.toStdString(), target_producer);
    logMessage(QString("Consumer added: id=%1, producer=%2")
                   .arg(QString::fromStdString(cid), QString::fromStdString(target_producer)));
}

void ServerTestWindow::onListProducers() {
    QString rid = sfu_room_input_->text().trimmed();
    if (rid.isEmpty()) { logMessage("Room ID is required"); return; }
    auto producers = sfu_mgr_.get_producers_json(rid.toStdString());
    logMessage(QString("--- Producers in '%1': %2 ---")
                   .arg(rid, QString::fromStdString(producers.dump())));
}

// ============================================================
// SignalingServer 测试
// ============================================================

void ServerTestWindow::onGenerateCert() {
    logMessage("Generating self-signed DTLS certificate...");
    int ret = std::system(
        "openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt "
        "-days 365 -nodes -subj '/CN=voice-sfu-server-test' 2>nul"
    );
    if (ret == 0 && QFile::exists("server.crt") && QFile::exists("server.key")) {
        logMessage("Certificate generated: server.crt + server.key");
    } else {
        logMessage("Certificate generation failed (openssl not available?)");
    }
}

void ServerTestWindow::onInitStartSignaling() {
    if (signaling_server_) {
        logMessage("SignalingServer already running, stop first");
        return;
    }

    // 确保 SFU 管理器已初始化
    sfu_mgr_.init(4010, 4020);

    // 初始化信令服务器
    auto server = std::make_unique<voice::sfu::SignalingServer>(nullptr);
    int port = signaling_port_input_->value();

    if (!server->init(static_cast<uint16_t>(port), room_mgr_, sfu_mgr_)) {
        logMessage("SignalingServer init failed (missing DTLS cert?)");
        logMessage("Hint: click 'Generate Cert' first, or ensure openssl is available");
        return;
    }

    if (!server->start()) {
        logMessage(QString("SignalingServer start failed on port %1").arg(port));
        return;
    }

    signaling_server_ = std::move(server);
    logMessage(QString("SignalingServer started on UDP port %1").arg(port));
    signaling_status_label_->setText("Status: Running");
    signaling_status_timer_->start(2000);
}

void ServerTestWindow::onStopSignaling() {
    if (!signaling_server_) {
        logMessage("SignalingServer is not running");
        return;
    }
    signaling_status_timer_->stop();
    signaling_server_->stop();
    sfu_mgr_.shutdown();
    logMessage("SignalingServer stopped");
    signaling_status_label_->setText("Status: Stopped");
    signaling_server_.reset();
}

void ServerTestWindow::onRefreshSignalingStatus() {
    if (!signaling_server_) {
        signaling_status_label_->setText("Status: Not running");
        return;
    }
    auto conns = signaling_server_->connection_count();
    signaling_status_label_->setText(
        QString("Running | Connections: %1").arg(conns));
}
