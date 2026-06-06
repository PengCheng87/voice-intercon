#pragma once

#include <cstdint>
#include <vector>
#include <string>

class DtlsSrtpSession {
public:
    DtlsSrtpSession();
    ~DtlsSrtpSession();

    bool init(bool is_server);
    void cleanup();

    bool process_dtls_packet(const uint8_t* data, size_t len, std::vector<uint8_t>& response);
    bool is_handshake_complete() const;

    bool encrypt_rtp(const uint8_t* payload, size_t payload_len, uint8_t* output, size_t* output_len);
    bool decrypt_rtp(const uint8_t* data, size_t len, uint8_t* output, size_t* output_len);

    std::string get_local_fingerprint() const;
    bool set_remote_fingerprint(const std::string& fingerprint);

private:
    bool is_server_;
    bool handshake_complete_;
    std::vector<uint8_t> key_material_;
    std::string local_fingerprint_;
    std::string remote_fingerprint_;
};
