#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dfone {

constexpr std::uint16_t kDefaultCommandPort = 49208;
constexpr std::uint16_t kDefaultDataPort = 49209;
constexpr std::size_t kPacketHeaderBytes = 32;
constexpr std::size_t kControlFrameBytes = 32;
constexpr std::size_t kPacketHeaderExtensionWords = 4;
constexpr std::uint32_t kCommandWriteReg = 0x0002;
constexpr std::uint32_t kCommandReadReg = 0x0003;
constexpr std::uint32_t kCommandFlagRead = 0x01;
constexpr std::uint32_t kCommandFlagWrite = 0x02;
constexpr std::size_t kPhaseCompCount = 7;
constexpr std::size_t kIqChannelCount = 8;

enum class PacketType : std::uint16_t {
    kControl = 0x5501,
    kResponse = 0x5502,
    kRxIq = 0x5503,
    kTxIq = 0x5504,
    kTxFlowControl = 0x5505,
    kCalibration = 0x5506,
    kRecordIq = 0x5507,
};

enum class CommandAddress : std::uint32_t {
    kSetRxGain = 0x0000,
    kSetSampleClockRate = 0x0001,
    kSetRxLoFreqLow = 0x0002,
    kSetRxLoFreqHigh = 0x0003,
    kGetIq = 0x0006,
    kGetUncorrectedIq = 0x001B,
    kSetRefClockSource = 0x001E,
    kSetRecordEnableChan = 0x0020,
    kGetRecordStorageHasSsd = 0x0021,
    kStartRecordMb = 0x0022,
    kSetRxLoFreq = 0x0031,
};

struct DeviceEndpoint {
    std::string device_ip = "192.168.7.2";
    std::uint16_t command_port = kDefaultCommandPort;
    std::uint16_t data_port = kDefaultDataPort;
};

struct StreamConfig {
    std::uint32_t sample_rate_hz = 30'720'000;
    std::uint32_t rx_gain_db = 0;
    std::uint32_t tx_atten_db = 0;
    std::uint32_t tx_cal_source = 0;
    std::uint32_t ref_clock_source = 0;
    std::uint64_t rx_lo_hz = 2'400'000'000ULL;
};

struct CaptureRequest {
    std::size_t sample_count = 65'536;
    std::size_t phase_repeat_count = 1;
    std::string output_path = "iq_once.cs16";
    bool save_to_file = false;
    bool continuous = false;
};

struct RecordRequest {
    std::uint8_t channel_mask = 0x1;
    std::uint32_t length_mb = 4;
    std::string output_path = "dfone_record.cs16";
};

using RecordProgressCallback = std::function<void(std::uint64_t, std::uint64_t)>;

struct PacketHeader {
    std::uint16_t magic_type = 0;
    std::uint16_t seq = 0;
    std::uint8_t sid = 0;
    std::uint32_t packet_len = 0;
    std::uint64_t timestamp = 0;
    std::array<std::uint32_t, kPacketHeaderExtensionWords> extension_words{};
};

struct IqCapture {
    PacketHeader header{};
    std::vector<std::uint8_t> payload;
};

struct CommandWord {
    std::uint32_t addr = 0;
    std::uint32_t data = 0;
};

}  // namespace dfone
