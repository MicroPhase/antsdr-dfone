#pragma once

#include "dfone/internal/protocol.hpp"
#include "dfone/internal/socket_compat.hpp"

namespace dfone {

class StreamClient {
public:
    explicit StreamClient(DeviceEndpoint endpoint);

    bool open();
    void close();

    bool is_open() const;
    bool read_capture_packet(IqCapture &capture);
    bool read_record_packets(const RecordRequest &request,
                             RecordProgressCallback progress_callback = {});

private:
    bool read_packet(PacketHeader &header, std::vector<std::uint8_t> &payload);

    DeviceEndpoint endpoint_;
    net::SocketHandle socket_fd_ = net::kInvalidSocket;
    bool open_ = false;
};

}  // namespace dfone
