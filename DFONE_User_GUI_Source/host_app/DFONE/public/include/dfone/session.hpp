#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "dfone/export.hpp"

namespace dfone {

constexpr std::size_t kDfOneApiChannelCount = 8;
constexpr std::size_t kDfOneApiBytesPerFrame = kDfOneApiChannelCount * 2 * sizeof(std::int16_t);

enum class DfOneWorkMode {
    kSynchronizedCapture,
    kIndependentAd9361,
};

enum class DfOneReferenceClock : std::uint32_t {
    kDefault = 0,
    kExternal = 1,
    kSource2 = 2,
    kSource3 = 3,
};

enum class DfOneIqKind {
    kCalibrated,
    kUncorrected,
};

enum class DfOneIqSampleFormat {
    kCs16LeInterleavedChannels,
};

struct DfOneEndpoint {
    std::string device_ip = "192.168.7.2";
    std::uint16_t command_port = 49208;
    std::uint16_t data_port = 49209;
};

struct DfOneCaptureConfig {
    DfOneWorkMode work_mode = DfOneWorkMode::kSynchronizedCapture;
    DfOneReferenceClock reference_clock = DfOneReferenceClock::kDefault;
    std::uint32_t sample_rate_hz = 30'720'000;
    std::uint64_t rx_lo_hz = 2'400'000'000ULL;
    std::uint32_t rx_gain_db = 30;
};

struct DfOneIqCapture {
    DfOneIqKind kind = DfOneIqKind::kCalibrated;
    DfOneIqSampleFormat sample_format = DfOneIqSampleFormat::kCs16LeInterleavedChannels;
    std::uint32_t sample_rate_hz = 0;
    std::uint64_t rx_lo_hz = 0;
    std::size_t frames = 0;
    std::size_t channel_count = kDfOneApiChannelCount;
    std::size_t bytes_per_frame = kDfOneApiBytesPerFrame;
    std::vector<std::uint8_t> payload;
};

struct DfOneRecordConfig {
    std::uint8_t channel_mask = 0x1;
    std::uint32_t length_mb = 4;
    std::string output_path = "dfone_record.cs16";
};

struct DfOneRecordProgress {
    double fraction = 0.0;
    std::uint64_t bytes_written = 0;
    std::uint64_t total_bytes = 0;
};

using DfOneRecordProgressCallback = std::function<void(const DfOneRecordProgress &)>;

class DfOneSession {
public:
    DFONE_API DfOneSession();
    DFONE_API explicit DfOneSession(const DfOneEndpoint &endpoint);
    DFONE_API explicit DfOneSession(const std::string &device_ip,
                                    std::uint16_t command_port = 49208,
                                    std::uint16_t data_port = 49209);
    DFONE_API ~DfOneSession();

    DfOneSession(const DfOneSession &) = delete;
    DfOneSession &operator=(const DfOneSession &) = delete;
    DFONE_API DfOneSession(DfOneSession &&) noexcept;
    DFONE_API DfOneSession &operator=(DfOneSession &&) noexcept;

    // Convenience API for user applications. These methods are thin aliases
    // over the explicit connect/configure/capture API below.
    DFONE_API bool open(const std::string &device_ip = "192.168.7.2",
                        std::uint16_t command_port = 49208,
                        std::uint16_t data_port = 49209);
    DFONE_API void close();
    DFONE_API bool is_open() const;

    DFONE_API bool set_sample_rate_hz(std::uint32_t sample_rate_hz);
    DFONE_API bool set_frequency_hz(std::uint64_t rx_lo_hz);
    DFONE_API bool set_gain_db(std::uint32_t gain_db);

    DFONE_API DfOneIqCapture get_iq(std::size_t frames);
    DFONE_API DfOneIqCapture get_uncorrected_iq(std::size_t frames);
    DFONE_API std::vector<std::uint8_t> get_iq_payload(std::size_t frames);
    DFONE_API std::vector<std::uint8_t> get_uncorrected_iq_payload(std::size_t frames);

    DFONE_API bool connect(const DfOneEndpoint &endpoint);
    DFONE_API void disconnect();
    DFONE_API bool connected() const;

    DFONE_API bool set_work_mode(DfOneWorkMode mode);
    DFONE_API bool configure(const DfOneCaptureConfig &config);
    DFONE_API bool set_reference_clock(DfOneReferenceClock source);
    DFONE_API bool set_rx_lo(std::uint64_t rx_lo_hz);
    DFONE_API bool set_rx_gain(std::uint32_t gain_db);
    DFONE_API bool set_sample_rate(std::uint32_t sample_rate_hz);
    DFONE_API bool get_record_storage_has_ssd(bool &has_ssd);

    DFONE_API bool capture_iq(std::size_t frames, DfOneIqCapture &out);
    DFONE_API bool capture_uncorrected_iq(std::size_t frames, DfOneIqCapture &out);
    DFONE_API bool record_iq(const DfOneRecordConfig &record);
    DFONE_API bool record_iq(const DfOneRecordConfig &record,
                             DfOneRecordProgressCallback progress_callback);

    DFONE_API const DfOneCaptureConfig &config() const;
    DFONE_API const std::string &last_error() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

using DfOneDevice = DfOneSession;

}  // namespace dfone
