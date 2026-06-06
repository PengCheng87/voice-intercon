#pragma once

#include <QObject>

// ============================================================
// PriorityMixer 测试
// ============================================================
class TestPriorityMixer : public QObject {
    Q_OBJECT
private slots:
    void testAddStream();
    void testRemoveStream();
    void testMixSilence();
    void testMixSingleStream();
    void testPriorityDominance();
    void testGainLimiting();
};

// ============================================================
// MediaStats 测试
// ============================================================
class TestMediaStats : public QObject {
    Q_OBJECT
private slots:
    void testInitialState();
    void testPacketReceived();
    void testPacketLoss();
    void testNoLossOnSequence();
    void testReset();
    void testLatency();
};

// ============================================================
// DTLS-SRTP 测试
// ============================================================
class TestDtlsSrtp : public QObject {
    Q_OBJECT
private slots:
    void testInitServer();
    void testInitClient();
    void testFingerprint();
    void testEncryptDecrypt();
    void testDataIntegrity();
};

// ============================================================
// RoomManager 测试
// ============================================================
class TestRoomManager : public QObject {
    Q_OBJECT
private slots:
    void testCreateRoom();
    void testDuplicateRoom();
    void testRemoveRoom();
    void testRemoveNonExistent();
    void testAddParticipant();
    void testAddParticipantNoRoom();
    void testRemoveParticipant();
    void testGetParticipant();
    void testParticipantsJson();
    void testMultipleRooms();
};

// ============================================================
// SFUManager 测试
// ============================================================
class TestSFUManager : public QObject {
    Q_OBJECT
private slots:
    void testInitShutdown();
    void testCreateTransport();
    void testAddProducer();
    void testRemoveProducer();
    void testAddConsumer();
    void testRemoveParticipantTransports();
    void testPortAllocation();
};

// ============================================================
// AudioManager 测试
// ============================================================
class TestAudioManager : public QObject {
    Q_OBJECT
private slots:
    void testInitialState();
    void testInit();
    void testDoubleInit();
    void testMuteToggle();
    void testSilenceToggle();
    void testCaptureLifecycle();
    void testPlaybackLifecycle();
    void testFeedPlaybackData();
    void testEncodeDecode();
    void testCaptureWithoutInit();
    void testPlaybackWithoutInit();
};
