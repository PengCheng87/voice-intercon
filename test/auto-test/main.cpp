/**
 * voice-intercom 自动化测试套件 (Qt Test)
 * 无 GUI 依赖，可在命令行和 CI 中运行
 * 用法: voice-auto-test [-silent] [-v1]
 *   返回 0 = 全部通过, 非 0 = 有失败
 */

#include <QTest>
#include <QCoreApplication>

#include <spdlog/spdlog.h>

#include "priority_mixer.h"
#include "media_stats.h"
#include "dtls_srtp.h"
#include "room_manager.h"
#include "sfu_manager.h"
#include "audio_manager.h"

#include <cmath>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>

#include "tests.h"

// ============================================================
// PriorityMixer 实现
// ============================================================

void TestPriorityMixer::testAddStream()
{
    PriorityMixer mixer;
    mixer.add_stream(0, Priority::P0_Alarm);
    mixer.add_stream(1, Priority::P1_Voice);
    mixer.add_stream(2, Priority::P2_Prompt);
    // 没有崩溃即通过
}

void TestPriorityMixer::testRemoveStream()
{
    PriorityMixer mixer;
    mixer.add_stream(0, Priority::P1_Voice);
    mixer.remove_stream(0);
    mixer.remove_stream(99); // 不存在的流：不应崩溃
}

void TestPriorityMixer::testMixSilence()
{
    PriorityMixer mixer;
    std::vector<float> output(960, -1.0f);
    mixer.mix(output.data(), 960);
    for (int i = 0; i < 960; ++i)
        QCOMPARE(output[i], 0.0f);
}

void TestPriorityMixer::testMixSingleStream()
{
    PriorityMixer mixer;
    mixer.add_stream(0, Priority::P1_Voice);
    std::vector<float> input(960, 0.5f);
    mixer.write_stream(0, input.data(), 960);
    std::vector<float> output(960, 0.0f);
    mixer.mix(output.data(), 960);
    float sum = 0;
    for (auto s : output) sum += std::abs(s);
    QVERIFY(sum > 0);
}

void TestPriorityMixer::testPriorityDominance()
{
    PriorityMixer mixer;
    mixer.add_stream(0, Priority::P2_Prompt);
    mixer.add_stream(1, Priority::P0_Alarm);
    std::vector<float> p2(960, 0.3f);
    std::vector<float> p0(960, 0.5f);
    mixer.write_stream(0, p2.data(), 960);
    mixer.write_stream(1, p0.data(), 960);
    std::vector<float> output(960);
    mixer.mix(output.data(), 960);
    QVERIFY(output[0] != 0.0f);
}

void TestPriorityMixer::testGainLimiting()
{
    PriorityMixer mixer;
    mixer.add_stream(0, Priority::P1_Voice);
    std::vector<float> input(960, 5.0f);
    mixer.write_stream(0, input.data(), 960);
    std::vector<float> output(960);
    mixer.mix(output.data(), 960);
    for (int i = 0; i < 960; ++i)
        QVERIFY(std::abs(output[i]) <= 1.0f);
}

// ============================================================
// MediaStats 实现
// ============================================================

void TestMediaStats::testInitialState()
{
    MediaStats stats;
    auto s = stats.get_stats();
    QCOMPARE(s.total_packets, 0u);
    QCOMPARE(s.lost_packets, 0u);
    QCOMPARE(s.packet_loss_rate, 0.0);
    QCOMPARE(s.avg_latency_ms, 0.0);
    QCOMPARE(s.jitter_ms, 0.0);
    QCOMPARE(s.mos_score, 0.0);
}

void TestMediaStats::testPacketReceived()
{
    MediaStats stats;
    stats.on_packet_received(1, 1000, 50);
    auto s = stats.get_stats();
    QCOMPARE(s.total_packets, 1u);
}

void TestMediaStats::testPacketLoss()
{
    MediaStats stats;
    stats.on_packet_received(1, 1000, 50);
    stats.on_packet_received(3, 1003, 50); // seq 2 lost
    auto s = stats.get_stats();
    QCOMPARE(s.total_packets, 2u);
    QCOMPARE(s.lost_packets, 1u);
    QVERIFY(s.packet_loss_rate > 0);
}

void TestMediaStats::testNoLossOnSequence()
{
    MediaStats stats;
    for (uint32_t i = 1; i <= 100; ++i)
        stats.on_packet_received(i, 1000 + i * 10, 50 + (i % 50));
    auto s = stats.get_stats();
    QCOMPARE(s.total_packets, 100u);
    QCOMPARE(s.lost_packets, 0u);
}

void TestMediaStats::testReset()
{
    MediaStats stats;
    stats.on_packet_received(1, 1000, 50);
    stats.reset();
    auto s = stats.get_stats();
    QCOMPARE(s.total_packets, 0u);
}

void TestMediaStats::testLatency()
{
    MediaStats stats;
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    // timestamp 应为发送时间戳，latency = 接收时间(now) - timestamp
    stats.on_packet_received(1, static_cast<uint64_t>(now - 50), 50);   // latency ~50ms
    stats.on_packet_received(2, static_cast<uint64_t>(now - 60), 60);   // latency ~60ms
    stats.on_packet_received(3, static_cast<uint64_t>(now - 40), 40);   // latency ~40ms
    auto s = stats.get_stats();
    QCOMPARE(s.total_packets, 3u);
    QVERIFY(s.avg_latency_ms >= 40.0 && s.avg_latency_ms <= 60.0);
}

// ============================================================
// DTLS-SRTP 实现
// ============================================================

void TestDtlsSrtp::testInitServer() {
    DtlsSrtpSession session;
    QVERIFY(session.init(true));
}

void TestDtlsSrtp::testInitClient() {
    DtlsSrtpSession session;
    QVERIFY(session.init(false));
}

void TestDtlsSrtp::testFingerprint() {
    DtlsSrtpSession session;
    session.init(true);
    QVERIFY(!session.get_local_fingerprint().empty());
}

void TestDtlsSrtp::testEncryptDecrypt()
{
    DtlsSrtpSession server, client;
    QVERIFY(server.init(true));
    QVERIFY(client.init(false));
    server.set_remote_fingerprint(client.get_local_fingerprint());
    client.set_remote_fingerprint(server.get_local_fingerprint());

    std::vector<uint8_t> dummy;
    server.process_dtls_packet(nullptr, 0, dummy);
    client.process_dtls_packet(nullptr, 0, dummy);
    QVERIFY(server.is_handshake_complete());
    QVERIFY(client.is_handshake_complete());

    uint8_t orig[] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08};
    uint8_t enc[8] = {}, dec[8] = {};
    size_t out = 0;
    QVERIFY(client.encrypt_rtp(orig, 8, enc, &out));
    QCOMPARE(out, (size_t)8);
    QVERIFY(server.decrypt_rtp(enc, out, dec, &out));
    QCOMPARE(out, (size_t)8);
    QCOMPARE(memcmp(orig, dec, 8), 0);
}

void TestDtlsSrtp::testDataIntegrity()
{
    DtlsSrtpSession server, client;
    server.init(true); client.init(false);
    server.set_remote_fingerprint(client.get_local_fingerprint());
    client.set_remote_fingerprint(server.get_local_fingerprint());
    std::vector<uint8_t> dummy;
    server.process_dtls_packet(nullptr, 0, dummy);
    client.process_dtls_packet(nullptr, 0, dummy);

    for (size_t len = 1; len <= 64; ++len) {
        std::vector<uint8_t> orig(len);
        for (size_t i = 0; i < len; ++i) orig[i] = static_cast<uint8_t>(i & 0xFF);
        std::vector<uint8_t> enc(len), dec(len);
        size_t out = 0;
        QVERIFY(client.encrypt_rtp(orig.data(), len, enc.data(), &out));
        QVERIFY(server.decrypt_rtp(enc.data(), out, dec.data(), &out));
        QCOMPARE(out, len);
        QCOMPARE(memcmp(orig.data(), dec.data(), len), 0);
    }
}

// ============================================================
// RoomManager 实现
// ============================================================

void TestRoomManager::testCreateRoom()
{
    voice::sfu::RoomManager mgr;
    QVERIFY(mgr.create_room("room_1"));
    QVERIFY(mgr.room_exists("room_1"));
    QCOMPARE(mgr.room_count(), (size_t)1);
}

void TestRoomManager::testDuplicateRoom()
{
    voice::sfu::RoomManager mgr;
    QVERIFY(mgr.create_room("room_1"));
    QVERIFY(!mgr.create_room("room_1"));
}

void TestRoomManager::testRemoveRoom()
{
    voice::sfu::RoomManager mgr;
    mgr.create_room("room_1");
    QVERIFY(mgr.remove_room("room_1"));
    QVERIFY(!mgr.room_exists("room_1"));
    QCOMPARE(mgr.room_count(), (size_t)0);
}

void TestRoomManager::testRemoveNonExistent()
{
    voice::sfu::RoomManager mgr;
    QVERIFY(!mgr.remove_room("no_such_room"));
}

void TestRoomManager::testAddParticipant()
{
    voice::sfu::RoomManager mgr;
    mgr.create_room("room_1");
    voice::sfu::Participant p;
    p.user_id = "user_a";
    p.role = voice::sfu::ParticipantRole::Doctor;
    QVERIFY(mgr.add_participant("room_1", p));
    QCOMPARE(mgr.participant_count("room_1"), (size_t)1);
}

void TestRoomManager::testAddParticipantNoRoom()
{
    voice::sfu::RoomManager mgr;
    voice::sfu::Participant p;
    p.user_id = "user_a";
    QVERIFY(!mgr.add_participant("no_room", p));
}

void TestRoomManager::testRemoveParticipant()
{
    voice::sfu::RoomManager mgr;
    mgr.create_room("room_1");
    voice::sfu::Participant p;
    p.user_id = "user_a";
    mgr.add_participant("room_1", p);
    QVERIFY(mgr.remove_participant("room_1", "user_a"));
    QCOMPARE(mgr.participant_count("room_1"), (size_t)0);
}

void TestRoomManager::testGetParticipant()
{
    voice::sfu::RoomManager mgr;
    mgr.create_room("room_1");
    voice::sfu::Participant p;
    p.user_id = "user_a";
    mgr.add_participant("room_1", p);
    auto* found = mgr.get_participant("room_1", "user_a");
    QVERIFY(found != nullptr);
    QCOMPARE(found->user_id, std::string("user_a"));
    QVERIFY(mgr.get_participant("room_1", "noone") == nullptr);
}

void TestRoomManager::testParticipantsJson()
{
    voice::sfu::RoomManager mgr;
    mgr.create_room("room_1");
    voice::sfu::Participant p;
    p.user_id = "user_a";
    p.role = voice::sfu::ParticipantRole::Doctor;
    mgr.add_participant("room_1", p);
    auto j = mgr.get_participants_json("room_1");
    QVERIFY(j.is_array());
    QCOMPARE(j.size(), (size_t)1);
    QCOMPARE(j[0]["user_id"].get<std::string>(), std::string("user_a"));
}

void TestRoomManager::testMultipleRooms()
{
    voice::sfu::RoomManager mgr;
    mgr.create_room("room_1");
    mgr.create_room("room_2");
    QCOMPARE(mgr.room_count(), (size_t)2);
    voice::sfu::Participant p;
    p.user_id = "user_a";
    mgr.add_participant("room_1", p);
    mgr.add_participant("room_2", p);
    QCOMPARE(mgr.participant_count("room_1"), (size_t)1);
    QCOMPARE(mgr.participant_count("room_2"), (size_t)1);
    mgr.remove_room("room_1");
    QCOMPARE(mgr.room_count(), (size_t)1);
}

// ============================================================
// SFUManager 实现
// ============================================================

void TestSFUManager::testInitShutdown()
{
    voice::sfu::SFUManager mgr;
    mgr.init(4000, 4010);
    mgr.shutdown();
}

void TestSFUManager::testCreateTransport()
{
    voice::sfu::SFUManager mgr;
    mgr.init(4000, 4010);
    auto info = mgr.create_transport("room_1", "user_a");
    QVERIFY(!info.transport_id.empty());
    QVERIFY(info.port >= 4000 && info.port <= 4010);
    mgr.shutdown();
}

void TestSFUManager::testAddProducer()
{
    voice::sfu::SFUManager mgr;
    mgr.init(4000, 4010);
    auto pid = mgr.add_producer("room_1", "user_a", voice::sfu::MediaType::Audio);
    QVERIFY(!pid.empty());
    auto j = mgr.get_producers_json("room_1");
    QVERIFY(j.is_array());
    QCOMPARE(j.size(), (size_t)1);
    mgr.shutdown();
}

void TestSFUManager::testRemoveProducer()
{
    voice::sfu::SFUManager mgr;
    mgr.init(4000, 4010);
    mgr.add_producer("room_1", "user_a", voice::sfu::MediaType::Audio);
    mgr.remove_producer("room_1", "user_a");
    QCOMPARE(mgr.get_producers_json("room_1").size(), (size_t)0);
    mgr.shutdown();
}

void TestSFUManager::testAddConsumer()
{
    voice::sfu::SFUManager mgr;
    mgr.init(4000, 4010);
    auto pid = mgr.add_producer("room_1", "user_a", voice::sfu::MediaType::Audio);
    auto cid = mgr.add_consumer("room_1", "user_b", pid);
    QVERIFY(!cid.empty());
    mgr.shutdown();
}

void TestSFUManager::testRemoveParticipantTransports()
{
    voice::sfu::SFUManager mgr;
    mgr.init(4000, 4010);
    mgr.create_transport("room_1", "user_a");
    mgr.add_producer("room_1", "user_a", voice::sfu::MediaType::Audio);
    mgr.remove_participant_transports("room_1", "user_a");
    QCOMPARE(mgr.get_producers_json("room_1").size(), (size_t)0);
    mgr.shutdown();
}

void TestSFUManager::testPortAllocation()
{
    voice::sfu::SFUManager mgr;
    mgr.init(5000, 5002);
    auto t1 = mgr.create_transport("r1", "u1");
    auto t2 = mgr.create_transport("r1", "u2");
    auto t3 = mgr.create_transport("r1", "u3");
    auto t4 = mgr.create_transport("r1", "u4"); // should wrap
    QCOMPARE(t1.port, 5000);
    QCOMPARE(t2.port, 5001);
    QCOMPARE(t3.port, 5002);
    QVERIFY(t4.port >= 5000);
    mgr.shutdown();
}

// ============================================================
// AudioManager 实现
// ============================================================

void TestAudioManager::testInitialState()
{
    voice::client::AudioManager mgr;
    QVERIFY(!mgr.is_initialized());
    QVERIFY(!mgr.is_capturing());
    QVERIFY(!mgr.is_playing());
    QVERIFY(!mgr.is_muted());
    QVERIFY(!mgr.is_silenced());
    QCOMPARE(mgr.get_audio_level(), 0.0f);
}

void TestAudioManager::testInit()
{
    voice::client::AudioManager mgr;
    QVERIFY(mgr.init("default", "default", 48000, 1));
    QVERIFY(mgr.is_initialized());
    mgr.cleanup();
    QVERIFY(!mgr.is_initialized());
}

void TestAudioManager::testDoubleInit()
{
    voice::client::AudioManager mgr;
    QVERIFY(mgr.init("default", "default", 48000, 1));
    QVERIFY(mgr.init("default", "default", 48000, 1)); // no-op
    mgr.cleanup();
}

void TestAudioManager::testMuteToggle()
{
    voice::client::AudioManager mgr;
    QVERIFY(!mgr.is_muted());
    mgr.set_mute(true);
    QVERIFY(mgr.is_muted());
    mgr.set_mute(false);
    QVERIFY(!mgr.is_muted());
}

void TestAudioManager::testSilenceToggle()
{
    voice::client::AudioManager mgr;
    QVERIFY(!mgr.is_silenced());
    mgr.set_silence(true);
    QVERIFY(mgr.is_silenced());
    mgr.set_silence(false);
    QVERIFY(!mgr.is_silenced());
}

void TestAudioManager::testCaptureLifecycle()
{
    voice::client::AudioManager mgr;
    mgr.init("default", "default", 48000, 1);
    QVERIFY(mgr.start_capture());
    QVERIFY(mgr.is_capturing());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    QVERIFY(mgr.stop_capture());
    QVERIFY(!mgr.is_capturing());
    mgr.cleanup();
}

void TestAudioManager::testPlaybackLifecycle()
{
    voice::client::AudioManager mgr;
    mgr.init("default", "default", 48000, 1);
    QVERIFY(mgr.start_playback());
    QVERIFY(mgr.is_playing());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    QVERIFY(mgr.stop_playback());
    QVERIFY(!mgr.is_playing());
    mgr.cleanup();
}

void TestAudioManager::testFeedPlaybackData()
{
    voice::client::AudioManager mgr;
    mgr.init("default", "default", 48000, 1);
    mgr.start_playback();
    std::vector<int16_t> data(960, 1000);
    mgr.feed_playback_data(data.data(), 960);
    mgr.stop_playback();
    mgr.cleanup();
}

void TestAudioManager::testEncodeDecode()
{
    voice::client::AudioManager mgr;
    mgr.init("default", "default", 48000, 1);
    std::vector<int16_t> pcm(480);
    for (int i = 0; i < 480; ++i)
        pcm[i] = static_cast<int16_t>(std::sin(2.0 * 3.14159 * 440.0 * i / 48000.0) * 10000);
    auto enc = mgr.encode_audio(pcm.data(), 480);
    QVERIFY(!enc.empty());
    auto dec = mgr.decode_audio(enc.data(), enc.size());
    QCOMPARE(dec.size(), pcm.size());
    mgr.cleanup();
}

void TestAudioManager::testCaptureWithoutInit()
{
    voice::client::AudioManager mgr;
    QVERIFY(!mgr.start_capture());
    QVERIFY(!mgr.is_capturing());
}

void TestAudioManager::testPlaybackWithoutInit()
{
    voice::client::AudioManager mgr;
    QVERIFY(!mgr.start_playback());
    QVERIFY(!mgr.is_playing());
}

// ============================================================
// 测试运行器
// ============================================================
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    // 关闭 spdlog 输出，避免干扰测试结果
    spdlog::set_level(spdlog::level::off);

    int result = 0;

    auto run = [&](QObject* obj, const char* name) {
        int r = QTest::qExec(obj, argc, argv);
        qDebug().noquote() << (r == 0 ? "[PASS]" : "[FAIL]") << name;
        result |= r;
    };

    TestPriorityMixer t_priority;
    TestMediaStats    t_media;
    TestDtlsSrtp      t_dtls;
    TestRoomManager   t_room;
    TestSFUManager    t_sfu;
    TestAudioManager  t_audio;

    run(&t_priority, "PriorityMixer");
    run(&t_media,    "MediaStats");
    run(&t_dtls,     "DTLS-SRTP");
    run(&t_room,     "RoomManager");
    run(&t_sfu,      "SFUManager");
    run(&t_audio,    "AudioManager");

    qDebug().noquote()
        << (result == 0 ? "\nAll tests PASSED" : "\nSome tests FAILED");

    return result;
}
