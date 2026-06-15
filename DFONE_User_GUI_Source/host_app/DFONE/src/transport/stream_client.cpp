#include "dfone/internal/stream_client.hpp"

#include <fstream>
#include <limits>
#include <sstream>

#include "dfone/internal/log.hpp"

namespace dfone {
namespace {

constexpr std::size_t kMaxPacketBytes = 64U * 1024U * 1024U;

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

}  // namespace

StreamClient::StreamClient(DeviceEndpoint endpoint)
    : endpoint_(endpoint)
{
}

bool StreamClient::open()
{
    if (open_) {
        return true;
    }

    if (!net::initialize_sockets()) {
        log(LogLevel::Error, "initialize WinSock failed");
        return false;
    }

    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!net::is_valid(socket_fd_)) {
        log(LogLevel::Error,
            std::string("create TCP data socket failed: ") + net::last_error_message());
        return false;
    }

    net::set_socket_timeouts(socket_fd_, 10000);

    const int recv_buf = 8 << 20;
    const int send_buf = 1 << 20;
    const int enable = 1;
    net::set_socket_buffer(socket_fd_, SO_RCVBUF, recv_buf);
    net::set_socket_buffer(socket_fd_, SO_SNDBUF, send_buf);
    net::set_tcp_no_delay(socket_fd_, enable);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(endpoint_.data_port);
    if (::inet_pton(AF_INET, endpoint_.device_ip.c_str(), &addr.sin_addr) != 1) {
        log(LogLevel::Error, "device-ip must be an IPv4 address");
        close();
        return false;
    }

    if (::connect(socket_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        log(LogLevel::Error,
            std::string("connect data socket failed: ") + net::last_error_message());
        close();
        return false;
    }

    open_ = true;

    std::ostringstream oss;
    oss << "connected to DFONE TCP data channel " << endpoint_.device_ip << ':'
        << endpoint_.data_port;
    log(LogLevel::Info, oss.str());
    return true;
}

void StreamClient::close()
{
    if (net::is_valid(socket_fd_)) {
        net::close_socket(socket_fd_);
        socket_fd_ = net::kInvalidSocket;
    }
    open_ = false;
}

bool StreamClient::is_open() const
{
    return open_;
}

bool StreamClient::read_capture_packet(IqCapture &capture)
{
    capture = {};
    PacketHeader header{};
    std::vector<std::uint8_t> payload;
    if (!read_packet(header, payload)) {
        return false;
    }

    const auto type = static_cast<PacketType>(header.magic_type);
    if (type != PacketType::kRxIq && type != PacketType::kCalibration) {
        log(LogLevel::Error, "unexpected packet type on data channel");
        return false;
    }

    capture.header = header;
    capture.payload = std::move(payload);
    return true;
}

bool StreamClient::read_record_packets(const RecordRequest &request,
                                       RecordProgressCallback progress_callback)
{
    if (!open_) {
        log(LogLevel::Error, "data channel is not open");
        return false;
    }
    if (request.output_path.empty()) {
        log(LogLevel::Error, "record output path is empty");
        return false;
    }

    std::ofstream out(request.output_path, std::ios::binary);
    if (!out) {
        log(LogLevel::Error, "open record output file failed: " + request.output_path);
        return false;
    }

    const std::uint64_t expected_bytes =
        static_cast<std::uint64_t>(request.length_mb) * 1024ULL * 1024ULL;
    std::uint64_t received_bytes = 0;
    if (progress_callback) {
        progress_callback(received_bytes, expected_bytes);
    }

    while (received_bytes < expected_bytes) {
        PacketHeader header{};
        std::vector<std::uint8_t> payload;
        if (!read_packet(header, payload)) {
            return false;
        }

        if (static_cast<PacketType>(header.magic_type) != PacketType::kRecordIq) {
            log(LogLevel::Error, "unexpected packet type while reading record IQ");
            return false;
        }
        if (payload.empty()) {
            log(LogLevel::Error, "empty record IQ packet");
            return false;
        }
        if (received_bytes + payload.size() > expected_bytes) {
            log(LogLevel::Error, "record IQ stream exceeded requested length");
            return false;
        }

        out.write(reinterpret_cast<const char *>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
        if (!out) {
            log(LogLevel::Error, "write record output file failed: " + request.output_path);
            return false;
        }
        received_bytes += payload.size();
        if (progress_callback) {
            progress_callback(received_bytes, expected_bytes);
        }
    }

    return true;
}

bool StreamClient::read_packet(PacketHeader &header, std::vector<std::uint8_t> &payload)
{
    if (!open_ || !net::is_valid(socket_fd_)) {
        log(LogLevel::Error, "data channel is not open");
        return false;
    }

    std::uint8_t raw_header[kPacketHeaderBytes] = {};
    const ReadExactResult header_read = read_exact(socket_fd_, raw_header, sizeof(raw_header));
    if (header_read != ReadExactResult::kOk) {
        log(LogLevel::Error,
            std::string("read data packet header failed: ") + read_failure_message(header_read));
        close();
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
        log(LogLevel::Error, "invalid DFONE data packet length");
        close();
        return false;
    }

    payload.resize(header.packet_len - kPacketHeaderBytes);
    if (!payload.empty()) {
        const ReadExactResult payload_read = read_exact(socket_fd_, payload.data(), payload.size());
        if (payload_read != ReadExactResult::kOk) {
            log(LogLevel::Error,
                std::string("read data packet payload failed: ") + read_failure_message(payload_read));
            close();
            return false;
        }
    }

    return true;
}

}  // namespace dfone
