#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dfone/internal/protocol.hpp"
#include "dfone/internal/socket_compat.hpp"

namespace dfone {

class StreamClient;

class ControlClient {
public:
    explicit ControlClient(DeviceEndpoint endpoint);

    bool connect();
    void disconnect();

    bool is_connected() const;
    const DeviceEndpoint &endpoint() const;

    bool set_rx_gain(std::uint32_t gain_db);
    bool set_ref_clock_source(std::uint32_t source);
    bool set_sample_rate(std::uint32_t sample_rate_hz);
    bool set_rx_lo_frequency(std::uint64_t rx_lo_hz);
    bool get_record_storage_has_ssd(bool &has_ssd);
    bool request_synchronized_iq(IqCapture &capture,
                                 StreamClient &stream,
                                 std::size_t sample_count = 0);
    bool request_uncorrected_iq(IqCapture &capture,
                                StreamClient &stream,
                                std::size_t sample_count = 0);
    bool start_record(const RecordRequest &request,
                      StreamClient &stream,
                      RecordProgressCallback progress_callback = {});
    bool send_command_raw(std::uint32_t addr, std::uint64_t value);
    bool request_iq_raw(std::uint32_t addr,
                        IqCapture &capture,
                        StreamClient &stream,
                        std::size_t sample_count = 0);

private:
    bool send_command(CommandAddress addr, std::uint64_t value);
    bool send_command_for_capture(CommandAddress addr,
                                  std::uint32_t value,
                                  StreamClient &stream,
                                  IqCapture &capture);
    bool send_command_frame(CommandAddress addr, std::uint64_t value);
    bool read_packet(PacketHeader &header, std::vector<std::uint8_t> &payload);
    bool read_response(std::uint64_t &response);

    DeviceEndpoint endpoint_;
    net::SocketHandle socket_fd_ = net::kInvalidSocket;
    std::uint16_t next_seq_ = 0;
    bool connected_ = false;
};

}  // namespace dfone
