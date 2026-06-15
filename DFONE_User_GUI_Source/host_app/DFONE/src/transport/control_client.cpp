#include "dfone/internal/control_client.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>

#include "dfone/internal/log.hpp"
#include "dfone/internal/stream_client.hpp"

namespace dfone {
namespace {

constexpr std::size_t kMaxPacketBytes = 64U * 1024U * 1024U;
constexpr std::uint8_t kDefaultSid = 0x70;

enum class ReadExactResult {
    kOk,
    kPeerClosed,
    kError,
};

std::uint32_t read_u32_le(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) |
           (static_cast<std::uint32_t>(p[3]) << 24U);
}

void write_u32_le(std::uint8_t *p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v & 0xffU);
    p[1] = static_cast<std::uint8_t>((v >> 8U) & 0xffU);
    p[2] = static_cast<std::uint8_t>((v >> 16U) & 0xffU);
    p[3] = static_cast<std::uint8_t>((v >> 24U) & 0xffU);
}

bool write_all(net::SocketHandle socket, const std::uint8_t *data, std::size_t len)
{
    std::size_t sent = 0;
    while (sent < len) {
        const int n = net::send_socket(socket, data + sent, len - sent);
        if (n < 0) {
            if (net::is_interrupted(net::last_error_code())) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

ReadExactResult read_exact(net::SocketHandle socket, std::uint8_t *data, std::size_t len)
{
    std::size_t got = 0;
    while (got < len) {
        const int n = net::recv_socket(socket, data + got, len - got);
        if (n < 0) {
            if (net::is_interrupted(net::last_error_code())) {
                continue;
            }
            return ReadExactResult::kError;
        }
        if (n == 0) {
            return ReadExactResult::kPeerClosed;
        }
        got += static_cast<std::size_t>(n);
    }
    return ReadExactResult::kOk;
}

std::string read_failure_message(ReadExactResult result)
{
    if (result == ReadExactResult::kPeerClosed) {
        return "peer closed connection";
    }
    return net::last_error_message();
}

std::uint32_t encode_capture_sample_count(std::size_t sample_count)
{
    if (sample_count == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(
        std::min<std::size_t>(sample_count, std::numeric_limits<std::uint32_t>::max()));
}

}  // namespace

ControlClient::ControlClient(DeviceEndpoint endpoint)
    : endpoint_(std::move(endpoint))
{
}

bool ControlClient::connect()
{
    if (connected_) {
        return true;
    }

    if (!net::initialize_sockets()) {
        log(LogLevel::Error, "initialize WinSock failed");
        return false;
    }

    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!net::is_valid(socket_fd_)) {
        log(LogLevel::Error,
            std::string("create TCP socket failed: ") + net::last_error_message());
        return false;
    }

    net::set_socket_timeouts(socket_fd_, 10000);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(endpoint_.command_port);
    if (::inet_pton(AF_INET, endpoint_.device_ip.c_str(), &addr.sin_addr) != 1) {
        log(LogLevel::Error, "device-ip must be an IPv4 address");
        disconnect();
        return false;
    }

    if (::connect(socket_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        log(LogLevel::Error, std::string("connect failed: ") + net::last_error_message());
        disconnect();
        return false;
    }

    connected_ = true;

    std::ostringstream oss;
    oss << "connected to DFONE TCP control/data channel " << endpoint_.device_ip << ':'
        << endpoint_.command_port;
    log(LogLevel::Info, oss.str());
    return true;
}

void ControlClient::disconnect()
{
    if (net::is_valid(socket_fd_)) {
        net::close_socket(socket_fd_);
        socket_fd_ = net::kInvalidSocket;
    }
    connected_ = false;
}

bool ControlClient::is_connected() const
{
    return connected_;
}

const DeviceEndpoint &ControlClient::endpoint() const
{
    return endpoint_;
}

bool ControlClient::set_rx_gain(std::uint32_t gain_db)
{
    return send_command(CommandAddress::kSetRxGain, gain_db);
}

bool ControlClient::set_ref_clock_source(std::uint32_t source)
{
    return send_command(CommandAddress::kSetRefClockSource, source & 0x3U);
}

bool ControlClient::set_sample_rate(std::uint32_t sample_rate_hz)
{
    return send_command(CommandAddress::kSetSampleClockRate, sample_rate_hz);
}

bool ControlClient::set_rx_lo_frequency(std::uint64_t rx_lo_hz)
{
    return send_command(CommandAddress::kSetRxLoFreq, rx_lo_hz);
}

bool ControlClient::get_record_storage_has_ssd(bool &has_ssd)
{
    has_ssd = false;
    if (!send_command_frame(CommandAddress::kGetRecordStorageHasSsd, 0)) {
        return false;
    }

    std::uint64_t response = 0;
    if (!read_response(response)) {
        return false;
    }

    has_ssd = (response & 0x1ULL) != 0;
    return true;
}

bool ControlClient::request_synchronized_iq(IqCapture &capture,
                                            StreamClient &stream,
                                            std::size_t sample_count)
{
    return send_command_for_capture(CommandAddress::kGetIq,
                                    encode_capture_sample_count(sample_count),
                                    stream,
                                    capture);
}

bool ControlClient::request_uncorrected_iq(IqCapture &capture,
                                           StreamClient &stream,
                                           std::size_t sample_count)
{
    return send_command_for_capture(CommandAddress::kGetUncorrectedIq,
                                    encode_capture_sample_count(sample_count),
                                    stream,
                                    capture);
}

bool ControlClient::start_record(const RecordRequest &request,
                                 StreamClient &stream,
                                 RecordProgressCallback progress_callback)
{
    bool has_ssd = false;
    if (!get_record_storage_has_ssd(has_ssd)) {
        return false;
    }

    const auto mask = static_cast<std::uint32_t>(request.channel_mask & 0x0FU);
    if (mask != 0x1U && mask != 0x3U && mask != 0xFU) {
        log(LogLevel::Error, "unsupported record channel mask");
        return false;
    }
    if (request.length_mb == 0 || (request.length_mb % 4U) != 0) {
        log(LogLevel::Error, "record length must be a non-zero multiple of 4 MB");
        return false;
    }

    const std::uint32_t max_mb = has_ssd ? (1024U * 1024U) : 1024U;
    if (request.length_mb > max_mb) {
        log(LogLevel::Error, "record length exceeds selected storage limit");
        return false;
    }

    if (!send_command(CommandAddress::kSetRecordEnableChan, mask)) {
        return false;
    }
    if (!send_command_frame(CommandAddress::kStartRecordMb, request.length_mb)) {
        return false;
    }

    std::uint64_t response = 1;
    if (!read_response(response) || response != 0) {
        log(LogLevel::Error, "record start command returned non-zero response");
        return false;
    }

    return stream.read_record_packets(request, std::move(progress_callback));
}

bool ControlClient::send_command_raw(std::uint32_t addr, std::uint64_t value)
{
    return send_command(static_cast<CommandAddress>(addr), value);
}

bool ControlClient::request_iq_raw(std::uint32_t addr,
                                   IqCapture &capture,
                                   StreamClient &stream,
                                   std::size_t sample_count)
{
    return send_command_for_capture(static_cast<CommandAddress>(addr),
                                    encode_capture_sample_count(sample_count),
                                    stream,
                                    capture);
}

bool ControlClient::send_command(CommandAddress addr, std::uint64_t value)
{
    if (!send_command_frame(addr, value)) {
        return false;
    }

    std::uint64_t response = 1;
    if (!read_response(response)) {
        return false;
    }

    if (response != 0) {
        std::ostringstream oss;
        oss << "command failed addr=0x" << std::hex << static_cast<std::uint32_t>(addr)
            << " response=0x" << response;
        log(LogLevel::Error, oss.str());
        return false;
    }

    return true;
}

bool ControlClient::send_command_for_capture(CommandAddress addr,
                                             std::uint32_t value,
                                             StreamClient &stream,
                                             IqCapture &capture)
{
    capture = {};
    if (!send_command_frame(addr, value)) {
        return false;
    }

    if (!stream.read_capture_packet(capture)) {
        return false;
    }

    std::uint64_t response = 1;
    if (!read_response(response)) {
        return false;
    }
    if (response != 0) {
        log(LogLevel::Error, "capture command returned non-zero response");
        return false;
    }

    return true;
}

bool ControlClient::send_command_frame(CommandAddress addr, std::uint64_t value)
{
    if (!connected_ || !net::is_valid(socket_fd_)) {
        log(LogLevel::Error, "control channel is not connected");
        return false;
    }

    std::uint8_t frame[kControlFrameBytes] = {};
    write_u32_le(frame + 0, (static_cast<std::uint32_t>(kDefaultSid) << 24U) |
                              static_cast<std::uint32_t>(kControlFrameBytes));
    write_u32_le(frame + 4, (static_cast<std::uint32_t>(PacketType::kControl) << 16U) |
                              static_cast<std::uint32_t>(next_seq_++));
    write_u32_le(frame + 8, 0);
    write_u32_le(frame + 12, 0);
    write_u32_le(frame + 16, kCommandWriteReg | (kCommandFlagWrite << 16U));
    write_u32_le(frame + 20, static_cast<std::uint32_t>(addr));
    write_u32_le(frame + 24, static_cast<std::uint32_t>(value & 0xffffffffULL));
    write_u32_le(frame + 28, static_cast<std::uint32_t>((value >> 32U) & 0xffffffffULL));

    if (!write_all(socket_fd_, frame, sizeof(frame))) {
        log(LogLevel::Error, std::string("send command failed: ") + net::last_error_message());
        disconnect();
        return false;
    }

    return true;
}

bool ControlClient::read_packet(PacketHeader &header, std::vector<std::uint8_t> &payload)
{
    std::uint8_t raw_header[kPacketHeaderBytes] = {};
    const ReadExactResult header_read = read_exact(socket_fd_, raw_header, sizeof(raw_header));
    if (header_read != ReadExactResult::kOk) {
        log(LogLevel::Error,
            std::string("read packet header failed: ") + read_failure_message(header_read));
        disconnect();
        return false;
    }

    const std::uint32_t word0 = read_u32_le(raw_header + 0);
    const std::uint32_t word1 = read_u32_le(raw_header + 4);
    const std::uint32_t word2 = read_u32_le(raw_header + 8);
    const std::uint32_t word3 = read_u32_le(raw_header + 12);

    header.sid = static_cast<std::uint8_t>(word0 >> 24U);
    header.packet_len = word0 & 0x00ffffffU;
    header.magic_type = static_cast<std::uint16_t>(word1 >> 16U);
    header.seq = static_cast<std::uint16_t>(word1 & 0xffffU);
    header.timestamp = static_cast<std::uint64_t>(word2) |
                       (static_cast<std::uint64_t>(word3) << 32U);
    for (std::size_t i = 0; i < header.extension_words.size(); ++i) {
        header.extension_words[i] = read_u32_le(raw_header + 16 + i * sizeof(std::uint32_t));
    }

    if (header.packet_len < kPacketHeaderBytes || header.packet_len > kMaxPacketBytes) {
        log(LogLevel::Error, "invalid DFONE packet length");
        disconnect();
        return false;
    }

    payload.resize(header.packet_len - kPacketHeaderBytes);
    if (!payload.empty()) {
        const ReadExactResult payload_read = read_exact(socket_fd_, payload.data(), payload.size());
        if (payload_read != ReadExactResult::kOk) {
            log(LogLevel::Error,
                std::string("read packet payload failed: ") + read_failure_message(payload_read));
            disconnect();
            return false;
        }
    }

    return true;
}

bool ControlClient::read_response(std::uint64_t &response)
{
    while (true) {
        PacketHeader header{};
        std::vector<std::uint8_t> payload;
        if (!read_packet(header, payload)) {
            return false;
        }

        if (static_cast<PacketType>(header.magic_type) != PacketType::kResponse) {
            log(LogLevel::Warn, "ignored non-response packet while waiting for command response");
            continue;
        }

        if (payload.size() >= sizeof(std::uint64_t)) {
            response = static_cast<std::uint64_t>(read_u32_le(payload.data())) |
                       (static_cast<std::uint64_t>(read_u32_le(payload.data() + 4)) << 32U);
            return true;
        }

        response = static_cast<std::uint64_t>(header.extension_words[0]) |
                   (static_cast<std::uint64_t>(header.extension_words[1]) << 32U);
        return true;
    }
}

}  // namespace dfone
