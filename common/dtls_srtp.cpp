#include "dtls_srtp.h"
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>

DtlsSrtpSession::DtlsSrtpSession()
    : is_server_(false)
    , handshake_complete_(false)
{
    key_material_.resize(32);
}

DtlsSrtpSession::~DtlsSrtpSession() {
    cleanup();
}

bool DtlsSrtpSession::init(bool is_server) {
    is_server_ = is_server;
    handshake_complete_ = false;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < key_material_.size(); ++i) {
        key_material_[i] = static_cast<uint8_t>(dis(gen));
    }

    std::stringstream ss;
    for (size_t i = 0; i < key_material_.size(); ++i) {
        if (i > 0) ss << ":";
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(key_material_[i]);
    }
    local_fingerprint_ = ss.str();

    return true;
}

void DtlsSrtpSession::cleanup() {
    handshake_complete_ = false;
    key_material_.clear();
}

bool DtlsSrtpSession::process_dtls_packet(const uint8_t* data, size_t len, std::vector<uint8_t>& response) {
    if (!handshake_complete_) {
        response.clear();
        handshake_complete_ = true;
        return true;
    }
    return false;
}

bool DtlsSrtpSession::is_handshake_complete() const {
    return handshake_complete_;
}

bool DtlsSrtpSession::encrypt_rtp(const uint8_t* payload, size_t payload_len, uint8_t* output, size_t* output_len) {
    if (!handshake_complete_) {
        return false;
    }

    if (payload_len > 0 && payload != nullptr) {
        for (size_t i = 0; i < payload_len; ++i) {
            output[i] = payload[i] ^ key_material_[i % key_material_.size()];
        }
        *output_len = payload_len;
        return true;
    }

    return false;
}

bool DtlsSrtpSession::decrypt_rtp(const uint8_t* data, size_t len, uint8_t* output, size_t* output_len) {
    if (!handshake_complete_) {
        return false;
    }

    if (len > 0 && data != nullptr) {
        for (size_t i = 0; i < len; ++i) {
            output[i] = data[i] ^ key_material_[i % key_material_.size()];
        }
        *output_len = len;
        return true;
    }

    return false;
}

std::string DtlsSrtpSession::get_local_fingerprint() const {
    return local_fingerprint_;
}

bool DtlsSrtpSession::set_remote_fingerprint(const std::string& fingerprint) {
    remote_fingerprint_ = fingerprint;
    return true;
}
