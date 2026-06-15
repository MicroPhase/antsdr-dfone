#include "dfone/session.hpp"

#include <stdexcept>
#include <utility>

#include "dfone/internal/control_client.hpp"
#include "dfone/internal/protocol.hpp"
#include "dfone/internal/stream_client.hpp"

namespace dfone {
namespace {

DeviceEndpoint to_internal_endpoint(const DfOneEndpoint &endpoint)
{
    DeviceEndpoint out;
    out.device_ip = endpoint.device_ip;
    out.command_port = endpoint.command_port;
    out.data_port = endpoint.data_port;
    return out;
}

RecordRequest to_internal_record_request(const DfOneRecordConfig &record)
{
    RecordRequest out;
    out.channel_mask = record.channel_mask;
    out.length_mb = record.length_mb;
    out.output_path = record.output_path;
    return out;
}

std::size_t payload_frames(const std::vector<std::uint8_t> &payload)
{
    return payload.size() / kDfOneApiBytesPerFrame;
}

}  // namespace

class DfOneSession::Impl {
public:
    bool connect(const DfOneEndpoint &endpoint)
    {
        disconnect();
        auto next_control = std::make_unique<ControlClient>(to_internal_endpoint(endpoint));
        auto next_stream = std::make_unique<StreamClient>(to_internal_endpoint(endpoint));
        if (!next_control->connect()) {
            last_error_ = "connect failed";
            return false;
        }
        if (!next_stream->open()) {
            next_control->disconnect();
            last_error_ = "connect data channel failed";
            return false;
        }

        control_ = std::move(next_control);
        stream_ = std::move(next_stream);
        endpoint_ = endpoint;
        config_ = {};
        last_error_.clear();
        return true;
    }

    void disconnect()
    {
        if (stream_) {
            stream_->close();
            stream_.reset();
        }
        if (control_) {
            control_->disconnect();
            control_.reset();
        }
    }

    bool connected() const
    {
        return control_ && control_->is_connected() && stream_ && stream_->is_open();
    }

    bool set_work_mode(DfOneWorkMode mode)
    {
        if (mode == DfOneWorkMode::kIndependentAd9361) {
            last_error_ = "independent AD9361 mode is not implemented";
            return false;
        }
        config_.work_mode = mode;
        last_error_.clear();
        return true;
    }

    bool configure(const DfOneCaptureConfig &config)
    {
        if (!require_connected()) {
            return false;
        }
        if (config.work_mode == DfOneWorkMode::kIndependentAd9361) {
            last_error_ = "independent AD9361 mode is not implemented";
            return false;
        }
        const auto ref_clock = static_cast<std::uint32_t>(config.reference_clock) & 0x3U;
        if (!control_->set_ref_clock_source(ref_clock)) {
            last_error_ = "set reference clock failed";
            return false;
        }
        if (!control_->set_sample_rate(config.sample_rate_hz)) {
            last_error_ = "set sample rate failed";
            return false;
        }
        if (!control_->set_rx_gain(config.rx_gain_db)) {
            last_error_ = "set RX gain failed";
            return false;
        }
        if (!control_->set_rx_lo_frequency(config.rx_lo_hz)) {
            last_error_ = "set RX LO failed";
            return false;
        }
        config_ = config;
        last_error_.clear();
        return true;
    }

    bool set_reference_clock(DfOneReferenceClock source)
    {
        if (!require_connected()) {
            return false;
        }
        const auto value = static_cast<std::uint32_t>(source) & 0x3U;
        if (!control_->set_ref_clock_source(value)) {
            last_error_ = "set reference clock failed";
            return false;
        }
        config_.reference_clock = source;
        last_error_.clear();
        return true;
    }

    bool set_rx_lo(std::uint64_t rx_lo_hz)
    {
        if (!require_connected()) {
            return false;
        }
        if (!control_->set_rx_lo_frequency(rx_lo_hz)) {
            last_error_ = "set RX LO failed";
            return false;
        }
        config_.rx_lo_hz = rx_lo_hz;
        last_error_.clear();
        return true;
    }

    bool set_rx_gain(std::uint32_t gain_db)
    {
        if (!require_connected()) {
            return false;
        }
        if (!control_->set_rx_gain(gain_db)) {
            last_error_ = "set RX gain failed";
            return false;
        }
        config_.rx_gain_db = gain_db;
        last_error_.clear();
        return true;
    }

    bool set_sample_rate(std::uint32_t sample_rate_hz)
    {
        if (!require_connected()) {
            return false;
        }
        if (!control_->set_sample_rate(sample_rate_hz)) {
            last_error_ = "set sample rate failed";
            return false;
        }
        config_.sample_rate_hz = sample_rate_hz;
        last_error_.clear();
        return true;
    }

    bool get_record_storage_has_ssd(bool &has_ssd)
    {
        has_ssd = false;
        if (!require_connected()) {
            return false;
        }
        if (!control_->get_record_storage_has_ssd(has_ssd)) {
            last_error_ = "get record SSD status failed";
            return false;
        }
        last_error_.clear();
        return true;
    }

    bool capture_iq(std::size_t frames, DfOneIqCapture &out)
    {
        if (!require_synchronized_mode()) {
            return false;
        }

        CaptureRequest request;
        request.sample_count = frames;
        request.output_path.clear();
        request.save_to_file = false;
        IqCapture capture;
        if (!control_->request_synchronized_iq(capture, *stream_, request.sample_count)) {
            last_error_ = "capture calibrated IQ failed";
            return false;
        }

        fill_capture(DfOneIqKind::kCalibrated, capture.payload, out);
        last_error_.clear();
        return true;
    }

    bool capture_uncorrected_iq(std::size_t frames, DfOneIqCapture &out)
    {
        if (!require_synchronized_mode()) {
            return false;
        }

        CaptureRequest request;
        request.sample_count = frames;
        request.output_path.clear();
        request.save_to_file = false;
        IqCapture capture;
        if (!control_->request_uncorrected_iq(capture, *stream_, request.sample_count)) {
            last_error_ = "capture uncorrected IQ failed";
            return false;
        }

        fill_capture(DfOneIqKind::kUncorrected, capture.payload, out);
        last_error_.clear();
        return true;
    }

    bool record_iq(const DfOneRecordConfig &record,
                   DfOneRecordProgressCallback progress_callback)
    {
        if (!require_synchronized_mode()) {
            return false;
        }

        RecordProgressCallback internal_progress;
        if (progress_callback) {
            internal_progress =
                [progress_callback = std::move(progress_callback)](
                    std::uint64_t bytes_written,
                    std::uint64_t total_bytes) {
                    DfOneRecordProgress progress;
                    progress.bytes_written = bytes_written;
                    progress.total_bytes = total_bytes;
                    progress.fraction =
                        total_bytes == 0
                            ? 0.0
                            : static_cast<double>(bytes_written) /
                                  static_cast<double>(total_bytes);
                    if (progress.fraction < 0.0) {
                        progress.fraction = 0.0;
                    } else if (progress.fraction > 1.0) {
                        progress.fraction = 1.0;
                    }
                    progress_callback(progress);
                };
        }

        if (!control_->start_record(to_internal_record_request(record),
                                    *stream_,
                                    std::move(internal_progress))) {
            last_error_ = "record IQ failed";
            return false;
        }

        last_error_.clear();
        return true;
    }

    const DfOneCaptureConfig &config() const
    {
        return config_;
    }

    const std::string &last_error() const
    {
        return last_error_;
    }

private:
    bool require_connected()
    {
        if (connected()) {
            return true;
        }
        last_error_ = "not connected";
        return false;
    }

    bool require_synchronized_mode()
    {
        if (!require_connected()) {
            return false;
        }
        if (config_.work_mode == DfOneWorkMode::kIndependentAd9361) {
            last_error_ = "independent AD9361 mode is not implemented";
            return false;
        }
        return true;
    }

    void fill_capture(DfOneIqKind kind,
                      const std::vector<std::uint8_t> &payload,
                      DfOneIqCapture &out) const
    {
        out.kind = kind;
        out.sample_format = DfOneIqSampleFormat::kCs16LeInterleavedChannels;
        out.sample_rate_hz = config_.sample_rate_hz;
        out.rx_lo_hz = config_.rx_lo_hz;
        out.frames = payload_frames(payload);
        out.channel_count = kDfOneApiChannelCount;
        out.bytes_per_frame = kDfOneApiBytesPerFrame;
        out.payload = payload;
    }

    DfOneEndpoint endpoint_{};
    DfOneCaptureConfig config_{};
    std::unique_ptr<ControlClient> control_;
    std::unique_ptr<StreamClient> stream_;
    std::string last_error_;
};

DfOneSession::DfOneSession()
    : impl_(std::make_unique<Impl>())
{
}

DfOneSession::DfOneSession(const DfOneEndpoint &endpoint)
    : DfOneSession()
{
    if (!connect(endpoint)) {
        throw std::runtime_error(last_error().empty() ? "connect failed" : last_error());
    }
}

DfOneSession::DfOneSession(const std::string &device_ip,
                           std::uint16_t command_port,
                           std::uint16_t data_port)
    : DfOneSession()
{
    if (!open(device_ip, command_port, data_port)) {
        throw std::runtime_error(last_error().empty() ? "connect failed" : last_error());
    }
}

DfOneSession::~DfOneSession() = default;
DfOneSession::DfOneSession(DfOneSession &&) noexcept = default;
DfOneSession &DfOneSession::operator=(DfOneSession &&) noexcept = default;

bool DfOneSession::open(const std::string &device_ip,
                        std::uint16_t command_port,
                        std::uint16_t data_port)
{
    DfOneEndpoint endpoint;
    endpoint.device_ip = device_ip;
    endpoint.command_port = command_port;
    endpoint.data_port = data_port;
    return connect(endpoint);
}

void DfOneSession::close()
{
    disconnect();
}

bool DfOneSession::is_open() const
{
    return connected();
}

bool DfOneSession::set_sample_rate_hz(std::uint32_t sample_rate_hz)
{
    return set_sample_rate(sample_rate_hz);
}

bool DfOneSession::set_frequency_hz(std::uint64_t rx_lo_hz)
{
    return set_rx_lo(rx_lo_hz);
}

bool DfOneSession::set_gain_db(std::uint32_t gain_db)
{
    return set_rx_gain(gain_db);
}

DfOneIqCapture DfOneSession::get_iq(std::size_t frames)
{
    DfOneIqCapture capture;
    (void)capture_iq(frames, capture);
    return capture;
}

DfOneIqCapture DfOneSession::get_uncorrected_iq(std::size_t frames)
{
    DfOneIqCapture capture;
    (void)capture_uncorrected_iq(frames, capture);
    return capture;
}

std::vector<std::uint8_t> DfOneSession::get_iq_payload(std::size_t frames)
{
    return get_iq(frames).payload;
}

std::vector<std::uint8_t> DfOneSession::get_uncorrected_iq_payload(std::size_t frames)
{
    return get_uncorrected_iq(frames).payload;
}

bool DfOneSession::connect(const DfOneEndpoint &endpoint)
{
    return impl_->connect(endpoint);
}

void DfOneSession::disconnect()
{
    impl_->disconnect();
}

bool DfOneSession::connected() const
{
    return impl_->connected();
}

bool DfOneSession::set_work_mode(DfOneWorkMode mode)
{
    return impl_->set_work_mode(mode);
}

bool DfOneSession::configure(const DfOneCaptureConfig &config)
{
    return impl_->configure(config);
}

bool DfOneSession::set_reference_clock(DfOneReferenceClock source)
{
    return impl_->set_reference_clock(source);
}

bool DfOneSession::set_rx_lo(std::uint64_t rx_lo_hz)
{
    return impl_->set_rx_lo(rx_lo_hz);
}

bool DfOneSession::set_rx_gain(std::uint32_t gain_db)
{
    return impl_->set_rx_gain(gain_db);
}

bool DfOneSession::set_sample_rate(std::uint32_t sample_rate_hz)
{
    return impl_->set_sample_rate(sample_rate_hz);
}

bool DfOneSession::get_record_storage_has_ssd(bool &has_ssd)
{
    return impl_->get_record_storage_has_ssd(has_ssd);
}

bool DfOneSession::capture_iq(std::size_t frames, DfOneIqCapture &out)
{
    return impl_->capture_iq(frames, out);
}

bool DfOneSession::capture_uncorrected_iq(std::size_t frames, DfOneIqCapture &out)
{
    return impl_->capture_uncorrected_iq(frames, out);
}

bool DfOneSession::record_iq(const DfOneRecordConfig &record)
{
    return impl_->record_iq(record, {});
}

bool DfOneSession::record_iq(const DfOneRecordConfig &record,
                             DfOneRecordProgressCallback progress_callback)
{
    return impl_->record_iq(record, std::move(progress_callback));
}

const DfOneCaptureConfig &DfOneSession::config() const
{
    return impl_->config();
}

const std::string &DfOneSession::last_error() const
{
    return impl_->last_error();
}

}  // namespace dfone
