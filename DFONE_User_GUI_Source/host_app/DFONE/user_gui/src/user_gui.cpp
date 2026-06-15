#define GLFW_INCLUDE_NONE
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"

#include "dfone/maintenance.hpp"
#include "dfone/session.hpp"

#ifdef DFONE_USER_ENABLE_DOA
#include "dfone/internal/doa_processing.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int kTextBufLen = 256;
constexpr std::size_t kPreviewSamples = 2048;
constexpr std::size_t kPreviewPhaseBlocks = 256;
constexpr std::size_t kPreviewPhaseBlockFrames = 2048;
constexpr std::size_t kChannelCount = dfone::kDfOneApiChannelCount;
constexpr int kSpectrumMinFftSize = 128;
constexpr int kSpectrumMaxFftSize = 8192;
constexpr double kPi = 3.14159265358979323846;
constexpr const char *kDefaultRecordOutputPath = "dfone_record.cs16";
constexpr const char *kDefaultBasebandOutputPath = "dfone_baseband_8ch.cs16";
constexpr std::size_t kBasebandSaveMaxChunkFrames = 262144;

namespace fs = std::filesystem;

constexpr ImU32 kChannelColors[kChannelCount] = {
    IM_COL32(238, 95, 91, 255),
    IM_COL32(245, 166, 35, 255),
    IM_COL32(247, 220, 92, 255),
    IM_COL32(103, 194, 58, 255),
    IM_COL32(55, 190, 190, 255),
    IM_COL32(64, 145, 255, 255),
    IM_COL32(161, 116, 255, 255),
    IM_COL32(237, 106, 194, 255),
};

struct AppState {
    struct PendingControlUpdate {
        bool has_reference_clock = false;
        dfone::DfOneReferenceClock reference_clock = dfone::DfOneReferenceClock::kDefault;
        bool has_sample_rate_hz = false;
        std::uint32_t sample_rate_hz = 0;
        bool has_rx_lo_hz = false;
        std::uint64_t rx_lo_hz = 0;
        bool has_rx_gain_db = false;
        std::uint32_t rx_gain_db = 0;
    };

    char device_ip[kTextBufLen] = "192.168.7.2";
    int command_port = 49208;
    int data_port = 49209;
    int reference_clock = 0;
    int rx_gain_db = 30;
    double sample_rate_msps = 15.36;
    double rx_lo_mhz = 2400.0;
    int frames = 65536;
    int capture_continuous_interval_ms = 0;
    bool save_to_file = false;
    char output_path[kTextBufLen] = "dfone_user_iq.cs16";
    int record_channel_index = 0;
    int record_length_mb = 4;
    bool record_has_ssd = false;
    char record_output_path[kTextBufLen] = "dfone_record.cs16";
    std::string record_auto_output_template = kDefaultRecordOutputPath;
    std::string record_auto_output_path;
    bool record_running = false;
    double record_progress = 0.0;
    std::uint64_t record_written_bytes = 0;
    std::uint64_t record_total_bytes = 0;
    std::string record_stage;
    int baseband_save_length_mb = 64;
    char baseband_save_output_path[kTextBufLen] = "dfone_baseband_8ch.cs16";
    std::string baseband_save_auto_output_template = kDefaultBasebandOutputPath;
    std::string baseband_save_auto_output_path;
    bool baseband_save_running = false;
    bool baseband_save_cancel = false;
    double baseband_save_progress = 0.0;
    std::uint64_t baseband_save_written_bytes = 0;
    std::uint64_t baseband_save_total_bytes = 0;
    std::string baseband_save_stage;
    int firmware_update_port = dfone::kDfOneFirmwareUpdatePort;
    int board_network_mode = 0;
    char board_network_mac[kTextBufLen] = "";
    char board_network_ip[kTextBufLen] = "192.168.1.10";
    char board_network_netmask[kTextBufLen] = "255.255.255.0";
    char board_network_gateway[kTextBufLen] = "";
    char board_network_dns[kTextBufLen] = "";
    char emmc_firmware_path[kTextBufLen] = "build/dfone/firmware/dfone-emmc.frm";
    char qspi_firmware_path[kTextBufLen] = "build/dfone/firmware/dfone-qspi.frm";
    bool firmware_update_reboot_after = true;
    bool busy = false;
    bool connected = false;
    bool capture_continuous_running = false;
    bool capture_continuous_cancel = false;
    std::uint64_t capture_continuous_iterations = 0;
    PendingControlUpdate pending_control_update{};
    bool firmware_update_running = false;
    double firmware_update_progress = 0.0;
    std::string firmware_update_stage;
    std::string board_id;
    std::string maintenance_output;
    std::string status = "Idle";
    std::string error;
    dfone::DfOneIqCapture last_capture;
#ifdef DFONE_USER_ENABLE_DOA
    bool doa_follow_rx_lo = true;
    double doa_center_freq_mhz = 2400.0;
    int doa_algorithm = 1;
    double doa_element_spacing_m = 0.0625;
    double doa_array_radius_m = 0.065;
    double doa_angle_min_deg = -180.0;
    double doa_angle_max_deg = 180.0;
    double doa_angle_step_deg = 1.0;
    int doa_start_sample = 0;
    int doa_sample_count = 4096;
    int doa_peak_count = 1;
    int doa_signal_count = 1;
    bool doa_remove_dc = true;
    bool doa_normalize_channels = true;
    bool doa_zc1_detection_enabled = false;
    int doa_zc1_channel = 0;
    int doa_zc1_fft_size = 1024;
    int doa_zc1_carriers = 601;
    int doa_zc1_cp_len = 72;
    int doa_zc1_root = 1;
    int doa_zc1_decimation = 1;
    double doa_zc1_threshold = 0.18;
    int doa_zc1_search_stride = 4;
    int doa_zc1_max_search_samples = 0;
    int doa_zc1_capture_samples = 2048;
    std::size_t doa_zc1_last_start = 0;
    double doa_zc1_last_score = 0.0;
    bool doa_zc1_last_detected = false;
    std::array<bool, kChannelCount> doa_enabled_channels = {
        true, true, true, true, true, true, true, true};
    bool doa_running = false;
    bool doa_cancel = false;
    std::uint64_t doa_iterations = 0;
    dfone::DfOneIqCapture doa_capture;
    std::array<std::array<float, kPreviewSamples>, kChannelCount> doa_preview_i{};
    std::size_t doa_preview_count = 0;
    dfone::DoaResult doa_result;
    std::string doa_error;
#endif
    std::array<std::array<float, kPreviewSamples>, kChannelCount> preview_i{};
    std::array<std::array<float, kPreviewSamples>, kChannelCount> preview_q{};
    std::array<std::array<float, kPreviewPhaseBlocks>, kChannelCount> phase_deg{};
    std::size_t preview_count = 0;
    std::size_t phase_count = 0;
    int spectrum_channel_index = 0;
    int spectrum_fft_size = 2048;
    std::vector<float> spectrum_freq_mhz;
    std::vector<float> spectrum_db;
    std::uint32_t spectrum_sample_rate_hz = 0;
    std::uint64_t spectrum_rx_lo_hz = 0;
    dfone::DfOneSession session;
    std::thread worker;
    std::mutex mutex;
};

struct AppSnapshot {
    std::string device_ip;
    int command_port = 49208;
    int data_port = 49209;
    int reference_clock = 0;
    int rx_gain_db = 30;
    double sample_rate_msps = 15.36;
    double rx_lo_mhz = 2400.0;
    int frames = 65536;
    int capture_continuous_interval_ms = 0;
    bool save_to_file = false;
    std::string output_path;
    int record_channel_index = 0;
    int record_length_mb = 4;
    bool record_has_ssd = false;
    std::string record_output_path;
    int baseband_save_length_mb = 64;
    std::string baseband_save_output_path;
    int firmware_update_port = dfone::kDfOneFirmwareUpdatePort;
    int board_network_mode = 0;
    std::string board_network_mac;
    std::string board_network_ip;
    std::string board_network_netmask;
    std::string board_network_gateway;
    std::string board_network_dns;
    std::string emmc_firmware_path;
    std::string qspi_firmware_path;
    bool firmware_update_reboot_after = true;
};

struct AppUiSnapshot {
    bool busy = false;
    bool connected = false;
    bool capture_continuous_running = false;
    std::uint64_t capture_continuous_iterations = 0;
    std::string status;
    std::string error;
    bool has_capture = false;
    dfone::DfOneIqKind capture_kind = dfone::DfOneIqKind::kCalibrated;
    std::uint32_t sample_rate_hz = 0;
    std::uint64_t rx_lo_hz = 0;
    std::size_t frames = 0;
    std::size_t bytes = 0;
#ifdef DFONE_USER_ENABLE_DOA
    bool doa_running = false;
    std::uint64_t doa_iterations = 0;
    dfone::DoaResult doa_result;
    std::string doa_error;
    bool doa_zc1_detected = false;
    std::size_t doa_zc1_start = 0;
    double doa_zc1_score = 0.0;
    std::array<std::array<float, kPreviewSamples>, kChannelCount> doa_preview_i{};
    std::size_t doa_preview_count = 0;
#endif
    std::array<std::array<float, kPreviewSamples>, kChannelCount> preview_i{};
    std::array<std::array<float, kPreviewSamples>, kChannelCount> preview_q{};
    std::array<std::array<float, kPreviewPhaseBlocks>, kChannelCount> phase_deg{};
    std::size_t preview_count = 0;
    std::size_t phase_count = 0;
    std::vector<float> spectrum_freq_mhz;
    std::vector<float> spectrum_db;
    std::uint32_t spectrum_sample_rate_hz = 0;
    std::uint64_t spectrum_rx_lo_hz = 0;
    bool record_running = false;
    double record_progress = 0.0;
    std::uint64_t record_written_bytes = 0;
    std::uint64_t record_total_bytes = 0;
    std::string record_stage;
    bool baseband_save_running = false;
    double baseband_save_progress = 0.0;
    std::uint64_t baseband_save_written_bytes = 0;
    std::uint64_t baseband_save_total_bytes = 0;
    std::string baseband_save_stage;
    bool firmware_update_running = false;
    double firmware_update_progress = 0.0;
    std::string firmware_update_stage;
    std::string board_id;
    std::string maintenance_output;
};

struct FilePickerState {
    fs::path current_dir;
    std::string error;
};

void apply_style()
{
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.WindowPadding = ImVec2(16.0f, 14.0f);

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.065f, 0.070f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.080f, 0.095f, 0.100f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.115f, 0.130f, 0.135f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.145f, 0.270f, 0.260f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.190f, 0.360f, 0.340f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.110f, 0.220f, 0.210f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.160f, 0.250f, 0.235f, 1.0f);
}

std::int16_t read_i16_le(const std::uint8_t *p)
{
    const std::uint16_t value = static_cast<std::uint16_t>(p[0]) |
                                (static_cast<std::uint16_t>(p[1]) << 8U);
    return static_cast<std::int16_t>(value);
}

using ComplexSample = std::complex<double>;

double wrap_degrees(double degrees)
{
    while (degrees > 180.0) {
        degrees -= 360.0;
    }
    while (degrees < -180.0) {
        degrees += 360.0;
    }
    return degrees;
}

int normalize_spectrum_fft_size(int value)
{
    value = std::max(kSpectrumMinFftSize, std::min(kSpectrumMaxFftSize, value));
    int fft_size = kSpectrumMinFftSize;
    while (fft_size < value && fft_size < kSpectrumMaxFftSize) {
        fft_size *= 2;
    }
    return fft_size;
}

void fft_in_place(std::vector<ComplexSample> &data)
{
    const std::size_t n = data.size();
    if (n < 2) {
        return;
    }

    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1U;
        for (; (j & bit) != 0; bit >>= 1U) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    for (std::size_t len = 2; len <= n; len <<= 1U) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const ComplexSample wlen(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            ComplexSample w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const ComplexSample u = data[i + j];
                const ComplexSample v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

void update_spectrum_locked(AppState &app)
{
    app.spectrum_freq_mhz.clear();
    app.spectrum_db.clear();
    app.spectrum_sample_rate_hz = app.last_capture.sample_rate_hz;
    app.spectrum_rx_lo_hz = app.last_capture.rx_lo_hz;
    app.spectrum_fft_size = normalize_spectrum_fft_size(app.spectrum_fft_size);

    const auto &capture = app.last_capture;
    const auto &payload = capture.payload;
    const std::size_t channel_count =
        std::min<std::size_t>(capture.channel_count, kChannelCount);
    const std::size_t frame_bytes = capture.bytes_per_frame;
    const std::size_t frames =
        std::min(capture.frames, frame_bytes > 0 ? payload.size() / frame_bytes : 0);
    if (channel_count == 0 || frame_bytes < channel_count * 4 || frames < 2 ||
        capture.sample_rate_hz == 0 || payload.size() < frame_bytes) {
        return;
    }

    const std::size_t channel =
        static_cast<std::size_t>(std::max(0, std::min<int>(
            static_cast<int>(channel_count) - 1, app.spectrum_channel_index)));
    app.spectrum_channel_index = static_cast<int>(channel);

    const std::size_t fft_size = static_cast<std::size_t>(app.spectrum_fft_size);
    const std::size_t used_frames = std::min(frames, fft_size);
    if (used_frames < 2) {
        return;
    }

    ComplexSample mean{};
    for (std::size_t frame = 0; frame < used_frames; ++frame) {
        const std::uint8_t *base = payload.data() + frame * frame_bytes + channel * 4;
        mean += ComplexSample(static_cast<double>(read_i16_le(base)),
                              static_cast<double>(read_i16_le(base + 2)));
    }
    mean /= static_cast<double>(used_frames);

    std::vector<ComplexSample> spectrum(fft_size);
    double coherent_gain = 0.0;
    for (std::size_t frame = 0; frame < used_frames; ++frame) {
        const double window =
            used_frames > 1
                ? 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(frame) /
                                        static_cast<double>(used_frames - 1))
                : 1.0;
        const std::uint8_t *base = payload.data() + frame * frame_bytes + channel * 4;
        const ComplexSample sample(static_cast<double>(read_i16_le(base)),
                                   static_cast<double>(read_i16_le(base + 2)));
        spectrum[frame] = (sample - mean) * window;
        coherent_gain += window;
    }

    fft_in_place(spectrum);

    app.spectrum_freq_mhz.resize(fft_size);
    app.spectrum_db.resize(fft_size);
    const double sample_rate_hz = static_cast<double>(capture.sample_rate_hz);
    const double center_hz = static_cast<double>(capture.rx_lo_hz);
    const double amplitude_scale = std::max(1.0, coherent_gain * 32768.0);
    for (std::size_t i = 0; i < fft_size; ++i) {
        const std::size_t bin = (i + fft_size / 2) % fft_size;
        const double offset_hz =
            (static_cast<double>(i) - static_cast<double>(fft_size) / 2.0) *
            sample_rate_hz / static_cast<double>(fft_size);
        const double magnitude =
            std::abs(spectrum[bin]) / amplitude_scale;
        app.spectrum_freq_mhz[i] =
            static_cast<float>((center_hz + offset_hz) / 1.0e6);
        app.spectrum_db[i] =
            static_cast<float>(20.0 * std::log10(std::max(magnitude, 1.0e-12)));
    }
}

void update_preview(AppState &app)
{
    std::lock_guard<std::mutex> lock(app.mutex);
    app.preview_count = 0;
    app.phase_count = 0;
    app.spectrum_freq_mhz.clear();
    app.spectrum_db.clear();
    const auto &payload = app.last_capture.payload;
    const std::size_t frame_bytes = app.last_capture.bytes_per_frame;
    if (frame_bytes < dfone::kDfOneApiBytesPerFrame || payload.size() < frame_bytes) {
        return;
    }

    const std::size_t frames = std::min({app.last_capture.frames,
                                         payload.size() / frame_bytes});
    const std::size_t preview_frames = std::min(frames, kPreviewSamples);
    for (std::size_t frame = 0; frame < preview_frames; ++frame) {
        const std::uint8_t *base = payload.data() + frame * frame_bytes;
        for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
            app.preview_i[ch][frame] = static_cast<float>(read_i16_le(base + ch * 4));
            app.preview_q[ch][frame] = static_cast<float>(read_i16_le(base + ch * 4 + 2));
        }
    }
    app.preview_count = preview_frames;
    update_spectrum_locked(app);

    if (frames < 2) {
        return;
    }

    const std::size_t phase_blocks =
        std::min(kPreviewPhaseBlocks,
                 (frames + kPreviewPhaseBlockFrames - 1) / kPreviewPhaseBlockFrames);
    for (std::size_t block = 0; block < phase_blocks; ++block) {
        const std::size_t begin = block * kPreviewPhaseBlockFrames;
        const std::size_t end = std::min(frames, begin + kPreviewPhaseBlockFrames);
        if (begin >= end) {
            break;
        }

        app.phase_deg[0][block] = 0.0f;
        for (std::size_t ch = 1; ch < kChannelCount; ++ch) {
            double sum_re = 0.0;
            double sum_im = 0.0;
            for (std::size_t frame = begin; frame < end; ++frame) {
                const std::uint8_t *base = payload.data() + frame * frame_bytes;
                const double i0 = static_cast<double>(read_i16_le(base));
                const double q0 = static_cast<double>(read_i16_le(base + 2));
                const double ix = static_cast<double>(read_i16_le(base + ch * 4));
                const double qx = static_cast<double>(read_i16_le(base + ch * 4 + 2));
                sum_re += ix * i0 + qx * q0;
                sum_im += qx * i0 - ix * q0;
            }
            const double degrees = std::atan2(sum_im, sum_re) * 180.0 / kPi;
            app.phase_deg[ch][block] = static_cast<float>(wrap_degrees(degrees));
        }
        app.phase_count = block + 1;
    }
}

#ifdef DFONE_USER_ENABLE_DOA
struct Zc1DetectorConfig {
    bool enabled = false;
    std::size_t channel = 0;
    int fft_size = 1024;
    int carriers = 601;
    int cp_len = 72;
    int root = 1;
    std::size_t decimation = 1;
    double threshold = 0.18;
    std::size_t search_stride = 4;
    std::size_t max_search_samples = 0;
    std::size_t capture_samples = 2048;
};

struct Zc1DetectionResult {
    bool detected = false;
    std::size_t start_sample = 0;
    std::size_t symbol_samples = 0;
    double score = 0.0;
    std::string error;
};

ComplexSample expj(double phase)
{
    return ComplexSample(std::cos(phase), std::sin(phase));
}

std::vector<ComplexSample> zadoff_chu_sequence(int root, int seq_length)
{
    std::vector<ComplexSample> out(static_cast<std::size_t>(seq_length));
    for (int n = 0; n < seq_length; ++n) {
        const double phase = -kPi * static_cast<double>(root) *
                             static_cast<double>(n) *
                             static_cast<double>(n + 1) /
                             static_cast<double>(seq_length);
        out[static_cast<std::size_t>(n)] = expj(phase);
    }
    return out;
}

std::vector<ComplexSample> make_dji_zc1_reference(const Zc1DetectorConfig &config)
{
    if (config.fft_size <= 0 || config.carriers <= 0 ||
        config.carriers >= config.fft_size || config.cp_len < 0 ||
        config.root <= 0) {
        return {};
    }

    std::vector<ComplexSample> freq(static_cast<std::size_t>(config.fft_size));
    auto zc = zadoff_chu_sequence(config.root, config.carriers);
    zc[static_cast<std::size_t>(config.carriers / 2)] = {};
    const int offset = static_cast<int>(
        std::ceil(static_cast<double>(config.fft_size - config.carriers) / 2.0));
    for (int i = 0; i < config.carriers; ++i) {
        freq[static_cast<std::size_t>(offset + i)] = zc[static_cast<std::size_t>(i)];
    }

    std::vector<ComplexSample> shifted(static_cast<std::size_t>(config.fft_size));
    const int half = config.fft_size / 2;
    for (int i = 0; i < config.fft_size; ++i) {
        const int src = (i + half) % config.fft_size;
        shifted[static_cast<std::size_t>(i)] = freq[static_cast<std::size_t>(src)];
    }

    std::vector<ComplexSample> time_domain(static_cast<std::size_t>(config.fft_size));
    for (int n = 0; n < config.fft_size; ++n) {
        ComplexSample sum{};
        for (int k = 0; k < config.fft_size; ++k) {
            const double phase = 2.0 * kPi * static_cast<double>(k * n) /
                                 static_cast<double>(config.fft_size);
            sum += shifted[static_cast<std::size_t>(k)] * expj(phase);
        }
        time_domain[static_cast<std::size_t>(n)] =
            sum / static_cast<double>(config.fft_size);
    }

    std::vector<ComplexSample> reference;
    reference.reserve(static_cast<std::size_t>(config.cp_len + config.fft_size));
    const int cp = std::min(config.cp_len, config.fft_size);
    for (int i = config.fft_size - cp; i < config.fft_size; ++i) {
        reference.push_back(time_domain[static_cast<std::size_t>(i)]);
    }
    reference.insert(reference.end(), time_domain.begin(), time_domain.end());
    return reference;
}

Zc1DetectorConfig make_zc1_detector_config(const AppState &app)
{
    Zc1DetectorConfig config;
    config.enabled = app.doa_zc1_detection_enabled;
    config.channel =
        static_cast<std::size_t>(std::max(0, std::min<int>(
            static_cast<int>(kChannelCount) - 1, app.doa_zc1_channel)));
    config.fft_size = std::max(16, app.doa_zc1_fft_size);
    config.carriers = std::max(1, app.doa_zc1_carriers);
    config.cp_len = std::max(0, app.doa_zc1_cp_len);
    config.root = std::max(1, app.doa_zc1_root);
    config.decimation =
        static_cast<std::size_t>(std::max(1, app.doa_zc1_decimation));
    config.threshold = std::max(0.0, app.doa_zc1_threshold);
    config.search_stride =
        static_cast<std::size_t>(std::max(1, app.doa_zc1_search_stride));
    config.max_search_samples =
        static_cast<std::size_t>(std::max(0, app.doa_zc1_max_search_samples));
    config.capture_samples =
        static_cast<std::size_t>(std::max(0, app.doa_zc1_capture_samples));
    return config;
}

Zc1DetectionResult detect_zc1_in_capture(const dfone::DfOneIqCapture &capture,
                                         const Zc1DetectorConfig &config)
{
    Zc1DetectionResult result;
    const std::size_t channel_count =
        std::min<std::size_t>(capture.channel_count, kChannelCount);
    const std::size_t frame_bytes = capture.bytes_per_frame;
    const std::size_t frames =
        std::min(capture.frames, frame_bytes > 0 ? capture.payload.size() / frame_bytes : 0);
    if (config.channel >= channel_count || frame_bytes < channel_count * 4 ||
        frames == 0) {
        result.error = "ZC1 detector input capture is invalid";
        return result;
    }

    const auto reference = make_dji_zc1_reference(config);
    if (reference.empty()) {
        result.error = "ZC1 detector parameters are invalid";
        return result;
    }
    result.symbol_samples = reference.size() * config.decimation;
    if (frames < result.symbol_samples) {
        result.error = "capture is shorter than one ZC1 reference symbol";
        return result;
    }

    double reference_power = 0.0;
    for (const auto &sample : reference) {
        reference_power += std::norm(sample);
    }
    if (reference_power <= 1.0e-18) {
        result.error = "ZC1 reference has zero power";
        return result;
    }

    const std::size_t searchable_frames_raw =
        config.max_search_samples == 0
            ? frames
            : std::min(frames, config.max_search_samples);
    if (searchable_frames_raw < result.symbol_samples) {
        result.error = "ZC1 search window is shorter than the reference";
        return result;
    }
    const std::size_t last_start_raw = searchable_frames_raw - result.symbol_samples;
    const std::size_t stride_raw =
        std::max<std::size_t>(1, config.search_stride * config.decimation);

    for (std::size_t phase = 0; phase < config.decimation && phase <= last_start_raw; ++phase) {
        for (std::size_t start = phase; start <= last_start_raw; start += stride_raw) {
            ComplexSample corr{};
            double window_power = 0.0;
            for (std::size_t n = 0; n < reference.size(); ++n) {
                const std::size_t sample_index = start + n * config.decimation;
                const std::uint8_t *base =
                    capture.payload.data() + sample_index * frame_bytes + config.channel * 4;
                const ComplexSample sample(static_cast<double>(read_i16_le(base)),
                                           static_cast<double>(read_i16_le(base + 2)));
                corr += sample * std::conj(reference[n]);
                window_power += std::norm(sample);
            }
            if (window_power <= 1.0e-18) {
                continue;
            }
            const double score = std::abs(corr) /
                                 std::sqrt(window_power * reference_power);
            if (score > result.score) {
                result.score = score;
                result.start_sample = start;
            }
        }
    }

    result.detected = result.score >= config.threshold;
    if (!result.detected) {
        result.error = "ZC1 not detected";
    }
    return result;
}

dfone::DfOneIqCapture slice_capture_for_doa(const dfone::DfOneIqCapture &capture,
                                           std::size_t start_sample,
                                           std::size_t sample_count)
{
    dfone::DfOneIqCapture out = capture;
    const std::size_t frame_bytes = capture.bytes_per_frame;
    const std::size_t frames =
        std::min(capture.frames, frame_bytes > 0 ? capture.payload.size() / frame_bytes : 0);
    if (frame_bytes == 0 || start_sample >= frames) {
        out.frames = 0;
        out.payload.clear();
        return out;
    }

    const std::size_t count =
        std::min(sample_count == 0 ? frames - start_sample : sample_count,
                 frames - start_sample);
    out.frames = count;
    out.payload.assign(capture.payload.begin() +
                           static_cast<std::ptrdiff_t>(start_sample * frame_bytes),
                       capture.payload.begin() +
                           static_cast<std::ptrdiff_t>((start_sample + count) * frame_bytes));
    return out;
}

void update_doa_preview_locked(AppState &app, const dfone::DfOneIqCapture &capture)
{
    app.doa_preview_count = 0;
    const auto &payload = capture.payload;
    const std::size_t frame_bytes = capture.bytes_per_frame;
    const std::size_t channel_count =
        std::min<std::size_t>(capture.channel_count, kChannelCount);
    if (channel_count == 0 || frame_bytes < channel_count * 4 ||
        payload.size() < frame_bytes) {
        return;
    }

    const std::size_t frames = std::min(capture.frames, payload.size() / frame_bytes);
    const std::size_t preview_frames = std::min(frames, kPreviewSamples);
    for (std::size_t frame = 0; frame < preview_frames; ++frame) {
        const std::uint8_t *base = payload.data() + frame * frame_bytes;
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            app.doa_preview_i[ch][frame] =
                static_cast<float>(read_i16_le(base + ch * 4));
        }
        for (std::size_t ch = channel_count; ch < kChannelCount; ++ch) {
            app.doa_preview_i[ch][frame] = 0.0f;
        }
    }
    app.doa_preview_count = preview_frames;
}
#endif

float clamp01(float v)
{
    return std::max(0.0f, std::min(1.0f, v));
}

std::uint32_t sample_rate_hz_from_msps(double msps)
{
    return static_cast<std::uint32_t>(std::max(1.0, msps * 1.0e6));
}

std::uint64_t rx_lo_hz_from_mhz(double mhz)
{
    return static_cast<std::uint64_t>(std::max(0.0, mhz * 1.0e6));
}

std::string path_for_input(const fs::path &path)
{
    std::error_code ec;
    const auto absolute_path = fs::absolute(path, ec);
    const auto &candidate = ec ? path : absolute_path;
    const auto cwd = fs::current_path(ec);
    if (!ec) {
        const auto relative_path = fs::relative(candidate, cwd, ec);
        if (!ec && !relative_path.empty()) {
            const auto rel = relative_path.generic_string();
            if (rel.rfind("..", 0) != 0) {
                return rel;
            }
        }
    }
    return candidate.generic_string();
}

void copy_path_to_buffer(const std::string &path, char *target, std::size_t target_size)
{
    if (!target || target_size == 0) {
        return;
    }
    std::snprintf(target, target_size, "%s", path.c_str());
}

std::string frequency_label_for_filename(double mhz)
{
    char text[64];
    std::snprintf(text, sizeof(text), "lo%.3fMHz", std::max(0.0, mhz));
    std::string out = text;
    std::replace(out.begin(), out.end(), '.', 'p');
    return out;
}

std::string timestamp_label_for_filename()
{
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    if (const std::tm *tm = std::localtime(&raw_time)) {
        local_time = *tm;
    }

    char date_time[32];
    if (std::strftime(date_time, sizeof(date_time), "%Y%m%d_%H%M%S", &local_time) == 0) {
        std::snprintf(date_time, sizeof(date_time), "00000000_000000");
    }

    char text[48];
    std::snprintf(text,
                  sizeof(text),
                  "%s_%03lld",
                  date_time,
                  static_cast<long long>(milliseconds));
    return text;
}

std::string make_timestamped_output_path(const std::string &template_path,
                                         const char *default_path,
                                         double rx_lo_mhz)
{
    const fs::path fallback(default_path && default_path[0] != '\0'
                                ? default_path
                                : kDefaultRecordOutputPath);
    const fs::path input_path(template_path.empty() ? fallback : fs::path(template_path));
    fs::path output_dir = input_path.parent_path();
    std::string stem = input_path.stem().generic_string();
    std::string extension = input_path.extension().generic_string();

    if (input_path.filename().empty()) {
        output_dir = input_path;
        stem = fallback.stem().generic_string();
        extension = fallback.extension().generic_string();
    }
    if (stem.empty()) {
        stem = fallback.stem().generic_string();
    }
    if (extension.empty()) {
        extension = fallback.extension().generic_string();
    }

    const std::string filename = stem + "_" +
                                 frequency_label_for_filename(rx_lo_mhz) + "_" +
                                 timestamp_label_for_filename() +
                                 extension;
    const fs::path output_path =
        output_dir.empty() ? fs::path(filename) : output_dir / filename;
    return output_path.generic_string();
}

void prepare_next_auto_output_path(std::string current_path,
                                   const char *default_path,
                                   double rx_lo_mhz,
                                   std::string &auto_template,
                                   std::string &last_auto_path,
                                   char *target,
                                   std::size_t target_size)
{
    if (current_path.empty()) {
        auto_template = default_path;
    } else if (current_path != last_auto_path) {
        auto_template = std::move(current_path);
    } else if (auto_template.empty()) {
        auto_template = default_path;
    }

    last_auto_path =
        make_timestamped_output_path(auto_template, default_path, rx_lo_mhz);
    copy_path_to_buffer(last_auto_path, target, target_size);
}

void prepare_next_record_output_path(AppState &app)
{
    prepare_next_auto_output_path(app.record_output_path,
                                  kDefaultRecordOutputPath,
                                  app.rx_lo_mhz,
                                  app.record_auto_output_template,
                                  app.record_auto_output_path,
                                  app.record_output_path,
                                  sizeof(app.record_output_path));
}

void prepare_next_baseband_save_output_path(AppState &app)
{
    prepare_next_auto_output_path(app.baseband_save_output_path,
                                  kDefaultBasebandOutputPath,
                                  app.rx_lo_mhz,
                                  app.baseband_save_auto_output_template,
                                  app.baseband_save_auto_output_path,
                                  app.baseband_save_output_path,
                                  sizeof(app.baseband_save_output_path));
}

bool is_existing_directory(const fs::path &path)
{
    std::error_code ec;
    return fs::is_directory(path, ec) && !ec;
}

fs::path nearest_existing_parent(fs::path path)
{
    while (!path.empty()) {
        if (is_existing_directory(path)) {
            return path;
        }
        const auto parent = path.parent_path();
        if (parent.empty() || parent == path) {
            break;
        }
        path = parent;
    }
    return {};
}

int existing_relative_prefix_count(const fs::path &root, const fs::path &relative_path)
{
    int count = 0;
    fs::path probe = root;
    for (const auto &part : relative_path) {
        if (part == "." || part.empty()) {
            continue;
        }
        if (part == "..") {
            return -1;
        }
        probe /= part;
        if (!is_existing_directory(probe)) {
            break;
        }
        ++count;
    }
    return count;
}

fs::path picker_start_dir_for_text(const char *current_text)
{
    std::error_code ec;
    const auto cwd = fs::current_path(ec);
    const fs::path fallback = ec ? fs::path(".") : cwd;
    const fs::path current_path(current_text ? current_text : "");
    if (current_path.empty()) {
        return fallback;
    }

    const fs::path base = current_path.has_parent_path()
                              ? current_path.parent_path()
                              : fs::path();
    if (base.empty()) {
        return fallback;
    }

    if (base.is_absolute()) {
        const auto exact = fs::absolute(base, ec);
        if (!ec && is_existing_directory(exact)) {
            return exact;
        }
        const auto parent = nearest_existing_parent(exact);
        return parent.empty() ? fallback : parent;
    }

    std::vector<fs::path> roots;
    for (fs::path root = fallback; !root.empty();) {
        roots.push_back(root);
        const auto parent = root.parent_path();
        if (parent.empty() || parent == root) {
            break;
        }
        root = parent;
    }

    for (const auto &root : roots) {
        const auto candidate = root / base;
        if (is_existing_directory(candidate)) {
            return candidate;
        }
    }

    int best_match = -1;
    fs::path best_dir;
    for (const auto &root : roots) {
        const int match = existing_relative_prefix_count(root, base);
        const auto candidate = nearest_existing_parent(root / base);
        if (!candidate.empty() && match > best_match) {
            best_match = match;
            best_dir = candidate;
        }
    }

    return best_dir.empty() ? fallback : best_dir;
}

void open_file_picker_for_current_text(FilePickerState &picker, const char *current_text)
{
    picker.current_dir = picker_start_dir_for_text(current_text);
    picker.error.clear();
}

void draw_file_picker_popup(const char *popup_id,
                            FilePickerState &state,
                            char *target,
                            std::size_t target_size)
{
    if (!ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    if (state.current_dir.empty()) {
        std::error_code ec;
        state.current_dir = fs::current_path(ec);
        if (ec) {
            state.current_dir = ".";
            state.error = ec.message();
        }
    } else if (!is_existing_directory(state.current_dir)) {
        const auto parent = nearest_existing_parent(state.current_dir);
        state.current_dir = parent.empty() ? fs::path(".") : parent;
    }

    ImGui::TextUnformatted("Current Directory");
    ImGui::TextWrapped("%s", state.current_dir.generic_string().c_str());
    if (ImGui::Button("Up", ImVec2(90, 0))) {
        const auto parent = state.current_dir.parent_path();
        if (!parent.empty()) {
            state.current_dir = parent;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Current Text", ImVec2(150, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 0))) {
        ImGui::CloseCurrentPopup();
    }

    std::vector<fs::directory_entry> dirs;
    std::vector<fs::directory_entry> files;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(state.current_dir, ec)) {
        std::error_code type_ec;
        if (entry.is_directory(type_ec)) {
            dirs.push_back(entry);
        } else if (!type_ec && entry.is_regular_file(type_ec)) {
            files.push_back(entry);
        }
    }
    if (ec) {
        state.error = ec.message();
    } else {
        state.error.clear();
    }

    auto by_name = [](const fs::directory_entry &a, const fs::directory_entry &b) {
        return a.path().filename().generic_string() < b.path().filename().generic_string();
    };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);

    ImGui::Separator();
    ImGui::BeginChild("file_list", ImVec2(620, 360), true);
    for (const auto &dir : dirs) {
        const auto label = std::string("[") + dir.path().filename().generic_string() + "]";
        if (ImGui::Selectable(label.c_str(), false)) {
            state.current_dir = dir.path();
        }
    }
    for (const auto &file : files) {
        const auto name = file.path().filename().generic_string();
        if (ImGui::Selectable(name.c_str(), false)) {
            copy_path_to_buffer(path_for_input(file.path()), target, target_size);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndChild();

    if (!state.error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "File browser error: %s",
                           state.error.c_str());
    }

    ImGui::EndPopup();
}

void draw_path_picker(const char *label,
                      const char *popup_id,
                      char *target,
                      std::size_t target_size,
                      FilePickerState &picker)
{
    const float button_width = 95.0f;
    const float label_width = ImGui::CalcTextSize(label).x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float input_width =
        std::max(170.0f, ImGui::GetContentRegionAvail().x - button_width -
                             label_width - spacing * 3.0f);
    ImGui::PushItemWidth(input_width);
    ImGui::InputText((std::string("##") + popup_id).c_str(), target, target_size);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button((std::string("Browse##") + popup_id).c_str(),
                      ImVec2(button_width, 0))) {
        open_file_picker_for_current_text(picker, target);
        ImGui::OpenPopup(popup_id);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    draw_file_picker_popup(popup_id, picker, target, target_size);
}

struct PlotView {
    bool initialized = false;
    std::size_t sample_count = 0;
    double data_min_x = 0.0;
    double data_max_x = 1.0;
    double data_min_y = 0.0;
    double data_max_y = 0.0;
    double x_min = 0.0;
    double x_max = 1.0;
    double y_min = -1.0;
    double y_max = 1.0;
};

ImVec2 map_plot_point_view(const ImVec2 &origin,
                           const ImVec2 &size,
                           double x,
                           double y,
                           const PlotView &view)
{
    const double x_range = std::max(view.x_max - view.x_min, 1.0);
    const double y_range = std::max(view.y_max - view.y_min, 1.0);
    const float x_norm = static_cast<float>((x - view.x_min) / x_range);
    const float y_norm = static_cast<float>((y - view.y_min) / y_range);
    return ImVec2(origin.x + x_norm * size.x, origin.y + (1.0f - y_norm) * size.y);
}

void clamp_x_view(PlotView &view, std::size_t count)
{
    if (count < 2) {
        view.x_min = 0.0;
        view.x_max = 1.0;
        return;
    }

    const double data_min = 0.0;
    const double data_max = static_cast<double>(count - 1);
    double range = view.x_max - view.x_min;
    if (range < 1.0) {
        const double center = (view.x_min + view.x_max) * 0.5;
        view.x_min = center - 0.5;
        view.x_max = center + 0.5;
        range = 1.0;
    }
    if (range >= data_max - data_min) {
        view.x_min = data_min;
        view.x_max = data_max;
        return;
    }
    if (view.x_min < data_min) {
        view.x_max += data_min - view.x_min;
        view.x_min = data_min;
    }
    if (view.x_max > data_max) {
        view.x_min -= view.x_max - data_max;
        view.x_max = data_max;
    }
}

void clamp_x_view_range(PlotView &view, double data_min, double data_max)
{
    if (data_max <= data_min) {
        view.x_min = data_min;
        view.x_max = data_min + 1.0;
        return;
    }

    double range = view.x_max - view.x_min;
    const double full_range = data_max - data_min;
    if (range < 1.0e-9) {
        const double center = (view.x_min + view.x_max) * 0.5;
        view.x_min = center - full_range * 0.005;
        view.x_max = center + full_range * 0.005;
        range = view.x_max - view.x_min;
    }
    if (range >= full_range) {
        view.x_min = data_min;
        view.x_max = data_max;
        return;
    }
    if (view.x_min < data_min) {
        view.x_max += data_min - view.x_min;
        view.x_min = data_min;
    }
    if (view.x_max > data_max) {
        view.x_min -= view.x_max - data_max;
        view.x_max = data_max;
    }
}

void clamp_y_view(PlotView &view, double data_min, double data_max)
{
    double range = view.y_max - view.y_min;
    const double full_range = data_max - data_min;
    if (range < 1.0) {
        const double center = (view.y_min + view.y_max) * 0.5;
        view.y_min = center - 0.5;
        view.y_max = center + 0.5;
        range = 1.0;
    }
    if (range >= full_range) {
        view.y_min = data_min;
        view.y_max = data_max;
        return;
    }
    if (view.y_min < data_min) {
        view.y_max += data_min - view.y_min;
        view.y_min = data_min;
    }
    if (view.y_max > data_max) {
        view.y_min -= view.y_max - data_max;
        view.y_max = data_max;
    }
}

void reset_plot_view_fixed_y(PlotView &view,
                             std::size_t count,
                             double min_y,
                             double max_y)
{
    view.initialized = true;
    view.sample_count = count;
    view.data_min_x = 0.0;
    view.data_max_x = count > 1 ? static_cast<double>(count - 1) : 1.0;
    view.data_min_y = min_y;
    view.data_max_y = max_y;
    view.x_min = 0.0;
    view.x_max = count > 1 ? static_cast<double>(count - 1) : 1.0;
    view.y_min = min_y;
    view.y_max = max_y;
}

void reset_plot_view_auto_y(PlotView &view,
                            std::size_t count,
                            double min_y,
                            double max_y,
                            double hard_min,
                            double hard_max)
{
    if (min_y > max_y) {
        std::swap(min_y, max_y);
    }
    min_y = std::max(min_y, hard_min);
    max_y = std::min(max_y, hard_max);
    double center = (min_y + max_y) * 0.5;
    double half_range = std::max((max_y - min_y) * 0.5, 1.0);
    half_range *= 1.20;

    view.initialized = true;
    view.sample_count = count;
    view.data_min_x = 0.0;
    view.data_max_x = count > 1 ? static_cast<double>(count - 1) : 1.0;
    view.data_min_y = min_y;
    view.data_max_y = max_y;
    view.x_min = 0.0;
    view.x_max = count > 1 ? static_cast<double>(count - 1) : 1.0;
    view.y_min = center - half_range;
    view.y_max = center + half_range;
    clamp_y_view(view, hard_min, hard_max);
}

bool plot_data_bounds_changed(const PlotView &view, double min_y, double max_y)
{
    return std::abs(view.data_min_y - min_y) > 0.01 ||
           std::abs(view.data_max_y - max_y) > 0.01;
}

void draw_plot_grid(ImDrawList *draw_list, const ImVec2 &origin, const ImVec2 &size)
{
    const ImU32 border = IM_COL32(82, 100, 100, 255);
    const ImU32 grid = IM_COL32(48, 60, 60, 255);
    draw_list->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), border);
    for (int i = 1; i < 4; ++i) {
        const float x = origin.x + size.x * static_cast<float>(i) / 4.0f;
        const float y = origin.y + size.y * static_cast<float>(i) / 4.0f;
        draw_list->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), grid);
        draw_list->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y), grid);
    }
}

void draw_y_axis_labels(ImDrawList *draw_list,
                        const ImVec2 &origin,
                        const ImVec2 &size,
                        const PlotView &view,
                        const char *unit,
                        int decimals)
{
    const ImU32 text_color = IM_COL32(180, 205, 205, 255);
    for (int i = 0; i <= 4; ++i) {
        const double y = view.y_max - (view.y_max - view.y_min) * static_cast<double>(i) / 4.0;
        const float tick_y = origin.y + size.y * static_cast<float>(i) / 4.0f;
        char text[64];
        if (decimals > 0) {
            std::snprintf(text, sizeof(text), "%.*f%s", decimals, y, unit);
        } else {
            std::snprintf(text, sizeof(text), "%.0f%s", y, unit);
        }
        const ImVec2 text_size = ImGui::CalcTextSize(text);
        const float py = std::max(origin.y + 3.0f,
                                  std::min(tick_y - text_size.y * 0.5f,
                                           origin.y + size.y - text_size.y - 3.0f));
        draw_list->AddText(ImVec2(origin.x + 8.0f, py), text_color, text);
    }
}

void draw_hover_value(ImDrawList *draw_list,
                      const ImVec2 &origin,
                      const ImVec2 &size,
                      const PlotView &view,
                      const char *unit,
                      int decimals)
{
    if (!ImGui::IsItemHovered()) {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (mouse.x < origin.x || mouse.x > origin.x + size.x ||
        mouse.y < origin.y || mouse.y > origin.y + size.y) {
        return;
    }

    const double fx = clamp01((mouse.x - origin.x) / size.x);
    const double fy = clamp01((mouse.y - origin.y) / size.y);
    const double x = view.x_min + fx * std::max(view.x_max - view.x_min, 1.0);
    const double y = view.y_max - fy * std::max(view.y_max - view.y_min, 1.0);
    char text[96];
    if (decimals > 0) {
        std::snprintf(text, sizeof(text), "x=%.0f  y=%.*f%s", x, decimals, y, unit);
    } else {
        std::snprintf(text, sizeof(text), "x=%.0f  y=%.0f%s", x, y, unit);
    }
    const ImVec2 text_pos(mouse.x + 12.0f, mouse.y + 10.0f);
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    draw_list->AddRectFilled(text_pos,
                             ImVec2(text_pos.x + text_size.x + 8.0f,
                                    text_pos.y + text_size.y + 6.0f),
                             IM_COL32(7, 10, 11, 230),
                             3.0f);
    draw_list->AddText(ImVec2(text_pos.x + 4.0f, text_pos.y + 3.0f),
                       IM_COL32(230, 240, 240, 255),
                       text);
}

bool draw_plot_header(const char *title, const PlotView &view, const char *unit, int decimals)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    ImGui::SameLine();
    const bool reset_clicked = ImGui::SmallButton("Reset View");

    char range_text[160];
    if (decimals > 0) {
        std::snprintf(range_text,
                      sizeof(range_text),
                      "x:[%.0f, %.0f]   y:[%.*f%s, %.*f%s]",
                      view.x_min,
                      view.x_max,
                      decimals,
                      view.y_min,
                      unit,
                      decimals,
                      view.y_max,
                      unit);
    } else {
        std::snprintf(range_text,
                      sizeof(range_text),
                      "x:[%.0f, %.0f]   y:[%.0f%s, %.0f%s]",
                      view.x_min,
                      view.x_max,
                      view.y_min,
                      unit,
                      view.y_max,
                      unit);
    }

    ImGui::TextDisabled("wheel: zoom | left-drag: pan | double-click: reset");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", range_text);
    ImGui::Spacing();
    return reset_clicked;
}

void update_plot_interaction(PlotView &view,
                             std::size_t count,
                             const ImVec2 &origin,
                             const ImVec2 &canvas_size,
                             double min_y,
                             double max_y,
                             bool fixed_y)
{
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO &io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (fixed_y) {
            reset_plot_view_fixed_y(view, count, min_y, max_y);
        } else {
            reset_plot_view_auto_y(view, count, view.data_min_y, view.data_max_y, min_y, max_y);
        }
    }
    if (hovered && io.MouseWheel != 0.0f) {
        const ImVec2 mouse = io.MousePos;
        const double fx = clamp01((mouse.x - origin.x) / canvas_size.x);
        const double fy = clamp01((mouse.y - origin.y) / canvas_size.y);
        const double zoom = io.MouseWheel > 0.0f ? 0.82 : 1.22;

        const double x_range = std::max(view.x_max - view.x_min, 1.0);
        const double y_range = std::max(view.y_max - view.y_min, 1.0);
        const double x_at_mouse = view.x_min + fx * x_range;
        const double y_at_mouse = view.y_max - fy * y_range;
        const double new_x_range = std::max(1.0, x_range * zoom);
        const double new_y_range = std::max(1.0, y_range * zoom);

        view.x_min = x_at_mouse - fx * new_x_range;
        view.x_max = view.x_min + new_x_range;
        view.y_max = y_at_mouse + fy * new_y_range;
        view.y_min = view.y_max - new_y_range;
        clamp_x_view(view, count);
        clamp_y_view(view, min_y, max_y);
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const ImVec2 delta = io.MouseDelta;
        const double x_range = std::max(view.x_max - view.x_min, 1.0);
        const double y_range = std::max(view.y_max - view.y_min, 1.0);
        const double dx = static_cast<double>(delta.x) / std::max(canvas_size.x, 1.0f) * x_range;
        const double dy = static_cast<double>(delta.y) / std::max(canvas_size.y, 1.0f) * y_range;
        view.x_min -= dx;
        view.x_max -= dx;
        view.y_min += dy;
        view.y_max += dy;
        clamp_x_view(view, count);
        clamp_y_view(view, min_y, max_y);
    }
}

void update_plot_interaction_range(PlotView &view,
                                   double min_x,
                                   double max_x,
                                   const ImVec2 &origin,
                                   const ImVec2 &canvas_size,
                                   double min_y,
                                   double max_y)
{
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO &io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        view.x_min = min_x;
        view.x_max = max_x;
        view.y_min = min_y;
        view.y_max = max_y;
    }
    if (hovered && io.MouseWheel != 0.0f) {
        const ImVec2 mouse = io.MousePos;
        const double fx = clamp01((mouse.x - origin.x) / canvas_size.x);
        const double fy = clamp01((mouse.y - origin.y) / canvas_size.y);
        const double zoom = io.MouseWheel > 0.0f ? 0.82 : 1.22;

        const double x_range = std::max(view.x_max - view.x_min, 1.0e-9);
        const double y_range = std::max(view.y_max - view.y_min, 1.0);
        const double x_at_mouse = view.x_min + fx * x_range;
        const double y_at_mouse = view.y_max - fy * y_range;
        const double new_x_range = std::max(1.0e-9, x_range * zoom);
        const double new_y_range = std::max(1.0, y_range * zoom);

        view.x_min = x_at_mouse - fx * new_x_range;
        view.x_max = view.x_min + new_x_range;
        view.y_max = y_at_mouse + fy * new_y_range;
        view.y_min = view.y_max - new_y_range;
        clamp_x_view_range(view, min_x, max_x);
        clamp_y_view(view, min_y, max_y);
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const ImVec2 delta = io.MouseDelta;
        const double x_range = std::max(view.x_max - view.x_min, 1.0e-9);
        const double y_range = std::max(view.y_max - view.y_min, 1.0);
        const double dx = static_cast<double>(delta.x) / std::max(canvas_size.x, 1.0f) * x_range;
        const double dy = static_cast<double>(delta.y) / std::max(canvas_size.y, 1.0f) * y_range;
        view.x_min -= dx;
        view.x_max -= dx;
        view.y_min += dy;
        view.y_max += dy;
        clamp_x_view_range(view, min_x, max_x);
        clamp_y_view(view, min_y, max_y);
    }
}

void draw_channel_legend()
{
    ImGui::TextDisabled("Channels:");
    ImGui::SameLine();
    for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kChannelColors[ch]));
        ImGui::Text("CH%zu", ch);
        ImGui::PopStyleColor();
        if (ch + 1 < kChannelCount) {
            ImGui::SameLine();
        }
    }
}

void draw_multichannel_plot(const char *label,
                            const std::array<std::array<float, kPreviewSamples>, kChannelCount> &series,
                            std::size_t count,
                            float min_y,
                            float max_y,
                            float height)
{
    ImGui::PushID(label);
    if (count < 2) {
        ImGui::TextUnformatted(label);
        ImGui::TextDisabled("No plottable samples");
        ImGui::PopID();
        return;
    }

    static std::unordered_map<std::string, PlotView> views;
    PlotView &view = views[label];
    if (!view.initialized || view.sample_count != count ||
        view.data_min_y != min_y || view.data_max_y != max_y) {
        reset_plot_view_fixed_y(view, count, min_y, max_y);
    }
    if (draw_plot_header(label, view, "", 0)) {
        reset_plot_view_fixed_y(view, count, min_y, max_y);
    }

    const ImVec2 canvas_size(ImGui::GetContentRegionAvail().x, height);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("canvas", canvas_size);
    update_plot_interaction(view, count, origin, canvas_size, min_y, max_y, true);

    draw_list->AddRectFilled(origin,
                             ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
                             IM_COL32(16, 22, 23, 255));
    draw_plot_grid(draw_list, origin, canvas_size);
    draw_y_axis_labels(draw_list, origin, canvas_size, view, "", 0);

    draw_list->PushClipRect(origin,
                            ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
                            true);
    for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
        const std::size_t first = static_cast<std::size_t>(std::max(0.0, std::floor(view.x_min)));
        const std::size_t last = std::min(count - 1, static_cast<std::size_t>(std::ceil(view.x_max)));
        if (last <= first) {
            continue;
        }
        const std::size_t visible_count = last - first + 1;
        const std::size_t step = std::max<std::size_t>(1, visible_count / 2400U);
        std::size_t prev = first;
        for (std::size_t i = first + step; i <= last; i += step) {
            draw_list->AddLine(map_plot_point_view(origin, canvas_size, static_cast<double>(prev), series[ch][prev], view),
                               map_plot_point_view(origin, canvas_size, static_cast<double>(i), series[ch][i], view),
                               kChannelColors[ch],
                               1.2f);
            prev = i;
        }
        if (prev != last) {
            draw_list->AddLine(map_plot_point_view(origin, canvas_size, static_cast<double>(prev), series[ch][prev], view),
                               map_plot_point_view(origin, canvas_size, static_cast<double>(last), series[ch][last], view),
                               kChannelColors[ch],
                               1.2f);
        }
    }
    const ImVec2 zero = map_plot_point_view(origin, canvas_size, view.x_min, 0.0, view);
    if (zero.y >= origin.y && zero.y <= origin.y + canvas_size.y) {
        draw_list->AddLine(ImVec2(origin.x, zero.y),
                           ImVec2(origin.x + canvas_size.x, zero.y),
                           IM_COL32(120, 138, 138, 180));
    }
    draw_list->PopClipRect();
    draw_hover_value(draw_list, origin, canvas_size, view, "", 0);
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::PopID();
}

void draw_phase_plot(const char *label,
                     const std::array<std::array<float, kPreviewPhaseBlocks>, kChannelCount> &series,
                     std::size_t count)
{
    ImGui::PushID(label);
    if (count < 1) {
        ImGui::TextUnformatted(label);
        ImGui::TextDisabled("No phase data");
        ImGui::PopID();
        return;
    }

    float min_y = 0.0f;
    float max_y = 0.0f;
    bool have_samples = false;
    for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
        for (std::size_t i = 0; i < count; ++i) {
            const float v = series[ch][i];
            if (!have_samples) {
                min_y = v;
                max_y = v;
                have_samples = true;
            } else {
                min_y = std::min(min_y, v);
                max_y = std::max(max_y, v);
            }
        }
    }

    static std::unordered_map<std::string, PlotView> views;
    PlotView &view = views[label];
    constexpr double kPhaseMin = -180.0;
    constexpr double kPhaseMax = 180.0;
    if (!view.initialized || view.sample_count != count ||
        plot_data_bounds_changed(view, min_y, max_y)) {
        reset_plot_view_auto_y(view, count, min_y, max_y, kPhaseMin, kPhaseMax);
    }
    if (draw_plot_header(label, view, " deg", 1)) {
        reset_plot_view_auto_y(view, count, min_y, max_y, kPhaseMin, kPhaseMax);
    }

    const ImVec2 canvas_size(ImGui::GetContentRegionAvail().x, 210.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("phase_canvas", canvas_size);
    update_plot_interaction(view, count, origin, canvas_size, kPhaseMin, kPhaseMax, false);

    draw_list->AddRectFilled(origin,
                             ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
                             IM_COL32(16, 22, 23, 255));
    draw_plot_grid(draw_list, origin, canvas_size);
    draw_y_axis_labels(draw_list, origin, canvas_size, view, " deg", 0);

    draw_list->PushClipRect(origin,
                            ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
                            true);
    const ImVec2 zero = map_plot_point_view(origin, canvas_size, view.x_min, 0.0, view);
    if (zero.y >= origin.y && zero.y <= origin.y + canvas_size.y) {
        draw_list->AddLine(ImVec2(origin.x, zero.y),
                           ImVec2(origin.x + canvas_size.x, zero.y),
                           IM_COL32(120, 138, 138, 180));
    }

    for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
        if (count < 2) {
            const ImVec2 p =
                map_plot_point_view(origin, canvas_size, 0.0, series[ch][0], view);
            draw_list->AddCircleFilled(p, 2.5f, kChannelColors[ch]);
            continue;
        }
        const std::size_t first = static_cast<std::size_t>(std::max(0.0, std::floor(view.x_min)));
        const std::size_t last = std::min(count - 1, static_cast<std::size_t>(std::ceil(view.x_max)));
        if (last <= first) {
            continue;
        }
        const std::size_t visible_count = last - first + 1;
        const std::size_t step = std::max<std::size_t>(1, visible_count / 2400U);
        std::size_t prev = first;
        for (std::size_t i = first + step; i <= last; i += step) {
            draw_list->AddLine(map_plot_point_view(origin, canvas_size, static_cast<double>(prev), series[ch][prev], view),
                               map_plot_point_view(origin, canvas_size, static_cast<double>(i), series[ch][i], view),
                               kChannelColors[ch],
                               1.2f);
            prev = i;
        }
        if (prev != last) {
            draw_list->AddLine(map_plot_point_view(origin, canvas_size, static_cast<double>(prev), series[ch][prev], view),
                               map_plot_point_view(origin, canvas_size, static_cast<double>(last), series[ch][last], view),
                               kChannelColors[ch],
                               1.2f);
        }
    }
    draw_list->PopClipRect();
    draw_hover_value(draw_list, origin, canvas_size, view, " deg", 1);
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::PopID();
}

void refresh_spectrum(AppState &app)
{
    std::lock_guard<std::mutex> lock(app.mutex);
    update_spectrum_locked(app);
}

void draw_spectrum_hover_value(ImDrawList *draw_list,
                               const ImVec2 &origin,
                               const ImVec2 &size,
                               const PlotView &view)
{
    if (!ImGui::IsItemHovered()) {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (mouse.x < origin.x || mouse.x > origin.x + size.x ||
        mouse.y < origin.y || mouse.y > origin.y + size.y) {
        return;
    }

    const double fx = clamp01((mouse.x - origin.x) / size.x);
    const double fy = clamp01((mouse.y - origin.y) / size.y);
    const double x = view.x_min + fx * std::max(view.x_max - view.x_min, 1.0e-9);
    const double y = view.y_max - fy * std::max(view.y_max - view.y_min, 1.0);
    char text[128];
    std::snprintf(text, sizeof(text), "f=%.6f MHz  %.1f dB", x, y);
    const ImVec2 text_pos(mouse.x + 12.0f, mouse.y + 10.0f);
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    draw_list->AddRectFilled(text_pos,
                             ImVec2(text_pos.x + text_size.x + 8.0f,
                                    text_pos.y + text_size.y + 6.0f),
                             IM_COL32(7, 10, 11, 230),
                             3.0f);
    draw_list->AddText(ImVec2(text_pos.x + 4.0f, text_pos.y + 3.0f),
                       IM_COL32(230, 240, 240, 255),
                       text);
}

void draw_spectrum_plot(const AppUiSnapshot &ui,
                        int channel_index,
                        int fft_size,
                        float height)
{
    ImGui::PushID("spectrum_plot");
    if (ui.spectrum_freq_mhz.size() < 2 ||
        ui.spectrum_freq_mhz.size() != ui.spectrum_db.size()) {
        ImGui::TextDisabled("Capture IQ to show spectrum");
        ImGui::PopID();
        return;
    }

    double min_y = ui.spectrum_db.front();
    double max_y = ui.spectrum_db.front();
    for (const float value : ui.spectrum_db) {
        min_y = std::min(min_y, static_cast<double>(value));
        max_y = std::max(max_y, static_cast<double>(value));
    }
    min_y = std::max(-140.0, std::floor(min_y - 6.0));
    max_y = std::min(10.0, std::ceil(max_y + 6.0));
    if (max_y - min_y < 10.0) {
        const double center = (min_y + max_y) * 0.5;
        min_y = center - 5.0;
        max_y = center + 5.0;
    }

    const double min_x = ui.spectrum_freq_mhz.front();
    const double max_x = ui.spectrum_freq_mhz.back();
    static std::unordered_map<std::string, PlotView> views;
    PlotView &view = views["spectrum"];
    if (!view.initialized ||
        view.sample_count != ui.spectrum_db.size() ||
        std::abs(view.data_min_x - min_x) > 1.0e-6 ||
        std::abs(view.data_max_x - max_x) > 1.0e-6 ||
        plot_data_bounds_changed(view, min_y, max_y)) {
        view.initialized = true;
        view.sample_count = ui.spectrum_db.size();
        view.data_min_x = min_x;
        view.data_max_x = max_x;
        view.data_min_y = min_y;
        view.data_max_y = max_y;
        view.x_min = min_x;
        view.x_max = max_x;
        view.y_min = min_y;
        view.y_max = max_y;
    }

    char title[128];
    std::snprintf(title,
                  sizeof(title),
                  "CH%d Spectrum (%d-point FFT)",
                  channel_index,
                  fft_size);
    if (draw_plot_header(title, view, " dB", 1)) {
        view.x_min = min_x;
        view.x_max = max_x;
        view.y_min = min_y;
        view.y_max = max_y;
    }

    const ImVec2 canvas_size(ImGui::GetContentRegionAvail().x, height);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("spectrum_canvas", canvas_size);
    update_plot_interaction_range(view,
                                  min_x,
                                  max_x,
                                  origin,
                                  canvas_size,
                                  min_y,
                                  max_y);

    draw_list->AddRectFilled(origin,
                             ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
                             IM_COL32(16, 22, 23, 255));
    draw_plot_grid(draw_list, origin, canvas_size);
    draw_y_axis_labels(draw_list, origin, canvas_size, view, " dB", 0);

    draw_list->PushClipRect(origin,
                            ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
                            true);
    std::size_t first = 0;
    while (first + 1 < ui.spectrum_freq_mhz.size() &&
           ui.spectrum_freq_mhz[first] < view.x_min) {
        ++first;
    }
    if (first > 0) {
        --first;
    }
    std::size_t last = ui.spectrum_freq_mhz.size() - 1;
    while (last > 0 && ui.spectrum_freq_mhz[last] > view.x_max) {
        --last;
    }
    if (last + 1 < ui.spectrum_freq_mhz.size()) {
        ++last;
    }
    if (last > first) {
        const std::size_t visible_count = last - first + 1;
        const std::size_t step = std::max<std::size_t>(1, visible_count / 2400U);
        std::size_t prev = first;
        for (std::size_t i = first + step; i <= last; i += step) {
            draw_list->AddLine(
                map_plot_point_view(origin,
                                    canvas_size,
                                    ui.spectrum_freq_mhz[prev],
                                    ui.spectrum_db[prev],
                                    view),
                map_plot_point_view(origin,
                                    canvas_size,
                                    ui.spectrum_freq_mhz[i],
                                    ui.spectrum_db[i],
                                    view),
                kChannelColors[static_cast<std::size_t>(std::max(
                    0,
                    std::min<int>(static_cast<int>(kChannelCount) - 1,
                                  channel_index)))],
                1.5f);
            prev = i;
        }
        if (prev != last) {
            draw_list->AddLine(
                map_plot_point_view(origin,
                                    canvas_size,
                                    ui.spectrum_freq_mhz[prev],
                                    ui.spectrum_db[prev],
                                    view),
                map_plot_point_view(origin,
                                    canvas_size,
                                    ui.spectrum_freq_mhz[last],
                                    ui.spectrum_db[last],
                                    view),
                kChannelColors[static_cast<std::size_t>(std::max(
                    0,
                    std::min<int>(static_cast<int>(kChannelCount) - 1,
                                  channel_index)))],
                1.5f);
        }
    }
    draw_list->PopClipRect();
    draw_spectrum_hover_value(draw_list, origin, canvas_size, view);
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::PopID();
}

void draw_spectrum_panel(AppState &app, const AppUiSnapshot &ui)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Channel");
    ImGui::SameLine();
    bool changed = false;
    for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
        ImGui::PushID(static_cast<int>(ch));
        if (ImGui::RadioButton((std::string("CH") + std::to_string(ch)).c_str(),
                               app.spectrum_channel_index == static_cast<int>(ch))) {
            app.spectrum_channel_index = static_cast<int>(ch);
            changed = true;
        }
        ImGui::PopID();
        if (ch + 1 < kChannelCount) {
            ImGui::SameLine();
        }
    }

    ImGui::PushItemWidth(150.0f);
    ImGui::InputInt("FFT Size", &app.spectrum_fft_size, 0, 0);
    ImGui::PopItemWidth();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        app.spectrum_fft_size = normalize_spectrum_fft_size(app.spectrum_fft_size);
        changed = true;
    }
    app.spectrum_channel_index =
        std::max(0, std::min<int>(static_cast<int>(kChannelCount) - 1,
                                  app.spectrum_channel_index));
    app.spectrum_fft_size = normalize_spectrum_fft_size(app.spectrum_fft_size);
    if (changed) {
        refresh_spectrum(app);
    }

    ImGui::TextDisabled("sample_rate=%.6f MSPS  center=%.6f MHz",
                        static_cast<double>(ui.spectrum_sample_rate_hz) / 1.0e6,
                        static_cast<double>(ui.spectrum_rx_lo_hz) / 1.0e6);
    draw_spectrum_plot(ui,
                       app.spectrum_channel_index,
                       app.spectrum_fft_size,
                       360.0f);
}

#ifdef DFONE_USER_ENABLE_DOA
dfone::DoaAlgorithm doa_algorithm_from_index(int index)
{
    switch (index) {
    case 0:
        return dfone::DoaAlgorithm::kConventional;
    case 2:
        return dfone::DoaAlgorithm::kMusicCircular;
    case 3:
        return dfone::DoaAlgorithm::kCaponCircular;
    default:
        return dfone::DoaAlgorithm::kConventionalCircular;
    }
}

dfone::IqAnalysis build_doa_iq_analysis(const dfone::DfOneIqCapture &capture)
{
    dfone::IqAnalysis iq;
    iq.segment_len = kPreviewPhaseBlockFrames;

    const std::size_t channel_count =
        std::min<std::size_t>(capture.channel_count, kChannelCount);
    const std::size_t frame_bytes = capture.bytes_per_frame;
    if (channel_count == 0 || frame_bytes < channel_count * 4 ||
        capture.payload.size() < frame_bytes) {
        iq.error = "IQ payload is too small for DOA";
        return iq;
    }

    const std::size_t frames = std::min(capture.frames,
                                        capture.payload.size() / frame_bytes);
    if (frames == 0) {
        iq.error = "No IQ frames available for DOA";
        return iq;
    }

    iq.valid = true;
    iq.frames = frames;
    iq.plot_samples = std::min(frames, kPreviewSamples);
    iq.segments = frames / iq.segment_len;
    iq.i.assign(channel_count, std::vector<float>(frames));
    iq.q.assign(channel_count, std::vector<float>(frames));

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::uint8_t *base = capture.payload.data() + frame * frame_bytes;
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            iq.i[ch][frame] = static_cast<float>(read_i16_le(base + ch * 4));
            iq.q[ch][frame] = static_cast<float>(read_i16_le(base + ch * 4 + 2));
        }
    }

    return iq;
}

dfone::DoaConfig make_doa_config(const AppState &app, const dfone::DfOneIqCapture &capture)
{
    dfone::DoaConfig config;
    config.algorithm = doa_algorithm_from_index(app.doa_algorithm);
    const double followed_rx_lo_mhz = capture.rx_lo_hz > 0
                                          ? static_cast<double>(capture.rx_lo_hz) / 1.0e6
                                          : app.rx_lo_mhz;
    const double center_freq_mhz =
        app.doa_follow_rx_lo ? followed_rx_lo_mhz : app.doa_center_freq_mhz;
    config.center_freq_hz = std::max(0.000001, center_freq_mhz) * 1.0e6;
    config.element_spacing_m = std::max(0.000001, app.doa_element_spacing_m);
    config.array_radius_m = std::max(0.000001, app.doa_array_radius_m);
    config.angle_min_deg = app.doa_angle_min_deg;
    config.angle_max_deg = app.doa_angle_max_deg;
    config.angle_step_deg = std::max(0.01, app.doa_angle_step_deg);
    config.start_sample =
        static_cast<std::size_t>(std::max(0, app.doa_start_sample));
    config.sample_count =
        static_cast<std::size_t>(std::max(0, app.doa_sample_count));
    config.peak_count =
        static_cast<std::size_t>(std::max(1, std::min(8, app.doa_peak_count)));
    config.signal_count =
        static_cast<std::size_t>(std::max(1, std::min(7, app.doa_signal_count)));
    config.remove_dc = app.doa_remove_dc;
    config.normalize_channels = app.doa_normalize_channels;
    config.enabled_channels = app.doa_enabled_channels;
    return config;
}

dfone::DoaResult estimate_doa_for_capture(const dfone::DfOneIqCapture &capture,
                                          const dfone::DoaConfig &config)
{
    const dfone::IqAnalysis iq = build_doa_iq_analysis(capture);
    if (!iq.valid) {
        dfone::DoaResult result;
        result.error = iq.error;
        return result;
    }
    return dfone::estimate_doa(iq, config);
}

void draw_doa_spectrum_plot(const char *label, const dfone::DoaResult &result)
{
    ImGui::PushID(label);
    static std::unordered_map<std::string, PlotView> views;
    PlotView &view = views[label];

    if (result.angles_deg.size() < 2 ||
        result.angles_deg.size() != result.spectrum_db.size()) {
        if (!view.initialized) {
            view.initialized = true;
            view.sample_count = 361;
            view.data_min_x = -180.0;
            view.data_max_x = 180.0;
            view.data_min_y = -80.0;
            view.data_max_y = 5.0;
            view.x_min = -180.0;
            view.x_max = 180.0;
            view.y_min = -80.0;
            view.y_max = 5.0;
        }
        if (draw_plot_header(label, view, " dB", 1)) {
            view.x_min = view.data_min_x;
            view.x_max = view.data_max_x;
            view.y_min = view.data_min_y;
            view.y_max = view.data_max_y;
        }

        const ImVec2 canvas_size(ImGui::GetContentRegionAvail().x, 210.0f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        ImGui::InvisibleButton("doa_spectrum_canvas", canvas_size);
        update_plot_interaction_range(view,
                                      view.data_min_x,
                                      view.data_max_x,
                                      origin,
                                      canvas_size,
                                      view.data_min_y,
                                      view.data_max_y);

        draw_list->AddRectFilled(origin,
                                 ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
                                 IM_COL32(16, 22, 23, 255));
        draw_plot_grid(draw_list, origin, canvas_size);
        draw_y_axis_labels(draw_list, origin, canvas_size, view, " dB", 0);
        const char *message = "No DOA spectrum";
        const ImVec2 text_size = ImGui::CalcTextSize(message);
        draw_list->AddText(ImVec2(origin.x + (canvas_size.x - text_size.x) * 0.5f,
                                  origin.y + (canvas_size.y - text_size.y) * 0.5f),
                           IM_COL32(145, 156, 160, 255),
                           message);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::PopID();
        return;
    }

    double min_y = result.spectrum_db.front();
    double max_y = result.spectrum_db.front();
    for (const double value : result.spectrum_db) {
        min_y = std::min(min_y, value);
        max_y = std::max(max_y, value);
    }
    min_y = std::max(-80.0, std::floor(min_y - 3.0));
    max_y = std::min(5.0, std::ceil(max_y + 3.0));
    if (max_y - min_y < 1.0) {
        max_y = min_y + 1.0;
    }

    if (!view.initialized ||
        view.sample_count != result.angles_deg.size() ||
        std::abs(view.data_min_x - result.angles_deg.front()) > 0.01 ||
        std::abs(view.data_max_x - result.angles_deg.back()) > 0.01 ||
        plot_data_bounds_changed(view, min_y, max_y)) {
        view.initialized = true;
        view.sample_count = result.angles_deg.size();
        view.data_min_x = result.angles_deg.front();
        view.data_max_x = result.angles_deg.back();
        view.data_min_y = min_y;
        view.data_max_y = max_y;
        view.x_min = result.angles_deg.front();
        view.x_max = result.angles_deg.back();
        view.y_min = min_y;
        view.y_max = max_y;
    }
    if (draw_plot_header(label, view, " dB", 1)) {
        view.x_min = result.angles_deg.front();
        view.x_max = result.angles_deg.back();
        view.y_min = min_y;
        view.y_max = max_y;
    }

    const ImVec2 canvas_size(ImGui::GetContentRegionAvail().x, 210.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("doa_spectrum_canvas", canvas_size);
    update_plot_interaction_range(view,
                                  result.angles_deg.front(),
                                  result.angles_deg.back(),
                                  origin,
                                  canvas_size,
                                  min_y,
                                  max_y);

    draw_list->AddRectFilled(origin,
                             ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
                             IM_COL32(16, 22, 23, 255));
    draw_plot_grid(draw_list, origin, canvas_size);
    draw_y_axis_labels(draw_list, origin, canvas_size, view, " dB", 0);

    draw_list->PushClipRect(origin,
                            ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
                            true);
    std::size_t first = 0;
    while (first + 1 < result.angles_deg.size() &&
           result.angles_deg[first] < view.x_min) {
        ++first;
    }
    if (first > 0) {
        --first;
    }
    std::size_t last = result.angles_deg.size() - 1;
    while (last > 0 && result.angles_deg[last] > view.x_max) {
        --last;
    }
    if (last + 1 < result.angles_deg.size()) {
        ++last;
    }
    if (last > first) {
        const std::size_t visible_count = last - first + 1;
        const std::size_t step = std::max<std::size_t>(1, visible_count / 2400U);
        std::size_t prev = first;
        for (std::size_t i = first + step; i <= last; i += step) {
            draw_list->AddLine(
                map_plot_point_view(origin,
                                    canvas_size,
                                    result.angles_deg[prev],
                                    result.spectrum_db[prev],
                                    view),
                map_plot_point_view(origin,
                                    canvas_size,
                                    result.angles_deg[i],
                                    result.spectrum_db[i],
                                    view),
                IM_COL32(75, 190, 220, 255),
                1.5f);
            prev = i;
        }
        if (prev != last) {
            draw_list->AddLine(
                map_plot_point_view(origin,
                                    canvas_size,
                                    result.angles_deg[prev],
                                    result.spectrum_db[prev],
                                    view),
                map_plot_point_view(origin,
                                    canvas_size,
                                    result.angles_deg[last],
                                    result.spectrum_db[last],
                                    view),
                IM_COL32(75, 190, 220, 255),
                1.5f);
        }
    }
    for (const auto &peak : result.peaks) {
        const ImVec2 p = map_plot_point_view(origin,
                                             canvas_size,
                                             peak.angle_deg,
                                             peak.value_db,
                                             view);
        draw_list->AddCircleFilled(p, 4.0f, IM_COL32(245, 166, 35, 255));
        char text[64];
        std::snprintf(text, sizeof(text), "%.1f deg", peak.angle_deg);
        draw_list->AddText(ImVec2(p.x + 6.0f, p.y - 8.0f),
                           IM_COL32(240, 230, 205, 255),
                           text);
    }
    draw_list->PopClipRect();
    draw_hover_value(draw_list, origin, canvas_size, view, " dB", 1);
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::PopID();
}
#endif

bool write_capture_file(const char *path, const std::vector<std::uint8_t> &payload)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    if (!payload.empty()) {
        out.write(reinterpret_cast<const char *>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
    }
    return static_cast<bool>(out);
}

dfone::DfOneEndpoint make_endpoint(const AppSnapshot &snapshot)
{
    dfone::DfOneEndpoint endpoint;
    endpoint.device_ip = snapshot.device_ip;
    endpoint.command_port =
        static_cast<std::uint16_t>(std::max(1, std::min(65535, snapshot.command_port)));
    endpoint.data_port =
        static_cast<std::uint16_t>(std::max(1, std::min(65535, snapshot.data_port)));
    return endpoint;
}

dfone::DfOneCaptureConfig make_config(const AppSnapshot &snapshot)
{
    dfone::DfOneCaptureConfig config;
    config.work_mode = dfone::DfOneWorkMode::kSynchronizedCapture;
    config.reference_clock =
        static_cast<dfone::DfOneReferenceClock>(
            std::max(0, std::min(3, snapshot.reference_clock)));
    config.sample_rate_hz = sample_rate_hz_from_msps(snapshot.sample_rate_msps);
    config.rx_lo_hz = rx_lo_hz_from_mhz(snapshot.rx_lo_mhz);
    config.rx_gain_db =
        static_cast<std::uint32_t>(std::max(0, std::min(76, snapshot.rx_gain_db)));
    return config;
}

dfone::DfOneRecordConfig make_record_config(const AppSnapshot &snapshot)
{
    static constexpr std::uint8_t kRecordMasks[] = {0x1, 0x3, 0xF};
    const int index = std::max(0, std::min(2, snapshot.record_channel_index));

    dfone::DfOneRecordConfig config;
    config.channel_mask = kRecordMasks[index];
    config.length_mb =
        static_cast<std::uint32_t>(std::max(4, snapshot.record_length_mb));
    config.output_path = snapshot.record_output_path;
    return config;
}

dfone::DfOneMaintenanceEndpoint make_maintenance_endpoint(const AppSnapshot &snapshot)
{
    dfone::DfOneMaintenanceEndpoint endpoint;
    endpoint.device_ip = snapshot.device_ip;
    endpoint.firmware_update_port =
        static_cast<std::uint16_t>(std::max(1, std::min(65535,
                                                        snapshot.firmware_update_port)));
    return endpoint;
}

AppSnapshot snapshot_app(const AppState &app)
{
    AppSnapshot snapshot;
    snapshot.device_ip = app.device_ip;
    snapshot.command_port = app.command_port;
    snapshot.data_port = app.data_port;
    snapshot.reference_clock = app.reference_clock;
    snapshot.rx_gain_db = app.rx_gain_db;
    snapshot.sample_rate_msps = app.sample_rate_msps;
    snapshot.rx_lo_mhz = app.rx_lo_mhz;
    snapshot.frames = app.frames;
    snapshot.capture_continuous_interval_ms = app.capture_continuous_interval_ms;
    snapshot.save_to_file = app.save_to_file;
    snapshot.output_path = app.output_path;
    snapshot.record_channel_index = app.record_channel_index;
    snapshot.record_length_mb = app.record_length_mb;
    snapshot.record_has_ssd = app.record_has_ssd;
    snapshot.record_output_path = app.record_output_path;
    snapshot.baseband_save_length_mb = app.baseband_save_length_mb;
    snapshot.baseband_save_output_path = app.baseband_save_output_path;
    snapshot.firmware_update_port = app.firmware_update_port;
    snapshot.board_network_mode = app.board_network_mode;
    snapshot.board_network_mac = app.board_network_mac;
    snapshot.board_network_ip = app.board_network_ip;
    snapshot.board_network_netmask = app.board_network_netmask;
    snapshot.board_network_gateway = app.board_network_gateway;
    snapshot.board_network_dns = app.board_network_dns;
    snapshot.emmc_firmware_path = app.emmc_firmware_path;
    snapshot.qspi_firmware_path = app.qspi_firmware_path;
    snapshot.firmware_update_reboot_after = app.firmware_update_reboot_after;
    return snapshot;
}

AppUiSnapshot snapshot_ui(AppState &app)
{
    std::lock_guard<std::mutex> lock(app.mutex);
    AppUiSnapshot snapshot;
    snapshot.busy = app.busy;
    snapshot.connected = app.connected;
    snapshot.capture_continuous_running = app.capture_continuous_running;
    snapshot.capture_continuous_iterations = app.capture_continuous_iterations;
    snapshot.status = app.status;
    snapshot.error = app.error;
    snapshot.has_capture = !app.last_capture.payload.empty();
    snapshot.capture_kind = app.last_capture.kind;
    snapshot.sample_rate_hz = app.last_capture.sample_rate_hz;
    snapshot.rx_lo_hz = app.last_capture.rx_lo_hz;
    snapshot.frames = app.last_capture.frames;
    snapshot.bytes = app.last_capture.payload.size();
#ifdef DFONE_USER_ENABLE_DOA
    snapshot.doa_running = app.doa_running;
    snapshot.doa_iterations = app.doa_iterations;
    snapshot.doa_result = app.doa_result;
    snapshot.doa_error = app.doa_error;
    snapshot.doa_zc1_detected = app.doa_zc1_last_detected;
    snapshot.doa_zc1_start = app.doa_zc1_last_start;
    snapshot.doa_zc1_score = app.doa_zc1_last_score;
    snapshot.doa_preview_i = app.doa_preview_i;
    snapshot.doa_preview_count = app.doa_preview_count;
#endif
    snapshot.preview_i = app.preview_i;
    snapshot.preview_q = app.preview_q;
    snapshot.phase_deg = app.phase_deg;
    snapshot.preview_count = app.preview_count;
    snapshot.phase_count = app.phase_count;
    snapshot.spectrum_freq_mhz = app.spectrum_freq_mhz;
    snapshot.spectrum_db = app.spectrum_db;
    snapshot.spectrum_sample_rate_hz = app.spectrum_sample_rate_hz;
    snapshot.spectrum_rx_lo_hz = app.spectrum_rx_lo_hz;
    snapshot.record_running = app.record_running;
    snapshot.record_progress = app.record_progress;
    snapshot.record_written_bytes = app.record_written_bytes;
    snapshot.record_total_bytes = app.record_total_bytes;
    snapshot.record_stage = app.record_stage;
    snapshot.baseband_save_running = app.baseband_save_running;
    snapshot.baseband_save_progress = app.baseband_save_progress;
    snapshot.baseband_save_written_bytes = app.baseband_save_written_bytes;
    snapshot.baseband_save_total_bytes = app.baseband_save_total_bytes;
    snapshot.baseband_save_stage = app.baseband_save_stage;
    snapshot.firmware_update_running = app.firmware_update_running;
    snapshot.firmware_update_progress = app.firmware_update_progress;
    snapshot.firmware_update_stage = app.firmware_update_stage;
    snapshot.board_id = app.board_id;
    snapshot.maintenance_output = app.maintenance_output;
    return snapshot;
}

bool input_int_commit(const char *label, int *value)
{
    ImGui::InputInt(label, value, 0, 0);
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool input_double_commit(const char *label, double *value)
{
    ImGui::InputDouble(label, value, 0.0, 0.0, "%.6f");
    return ImGui::IsItemDeactivatedAfterEdit();
}

template <typename Fn>
void start_worker(AppState &app, const char *status, Fn fn)
{
    std::lock_guard<std::mutex> lock(app.mutex);
    if (app.busy) {
        return;
    }

    if (app.worker.joinable()) {
        app.worker.join();
    }

    app.busy = true;
    app.status = status;
    app.error.clear();

    app.worker = std::thread([&app, fn]() mutable {
        std::string local_error;
        bool ok = false;
        try {
            ok = fn(local_error);
        } catch (const std::exception &e) {
            local_error = e.what();
        } catch (...) {
            local_error = "unknown error";
        }

        std::lock_guard<std::mutex> done_lock(app.mutex);
        app.busy = false;
        app.status = ok ? "Ready" : "Failed";
        app.error = ok ? std::string{} : local_error;
    });
}

bool take_pending_control_update(AppState &app, AppState::PendingControlUpdate &update)
{
    std::lock_guard<std::mutex> lock(app.mutex);
    const auto &pending = app.pending_control_update;
    const bool has_update = pending.has_reference_clock || pending.has_sample_rate_hz ||
                            pending.has_rx_lo_hz || pending.has_rx_gain_db;
    if (has_update) {
        update = pending;
        app.pending_control_update = {};
        return true;
    }
    return false;
}

bool apply_pending_control_update(dfone::DfOneSession &session,
                                  const AppState::PendingControlUpdate &update,
                                  std::string &error)
{
    if (update.has_reference_clock && !session.set_reference_clock(update.reference_clock)) {
        error = session.last_error();
        return false;
    }
    if (update.has_sample_rate_hz && !session.set_sample_rate(update.sample_rate_hz)) {
        error = session.last_error();
        return false;
    }
    if (update.has_rx_lo_hz && !session.set_rx_lo(update.rx_lo_hz)) {
        error = session.last_error();
        return false;
    }
    if (update.has_rx_gain_db && !session.set_rx_gain(update.rx_gain_db)) {
        error = session.last_error();
        return false;
    }
    return true;
}

bool sleep_with_capture_cancel(AppState &app, int interval_ms)
{
    const int total_ms = std::max(0, interval_ms);
    int elapsed_ms = 0;
    while (elapsed_ms < total_ms) {
        {
            std::lock_guard<std::mutex> lock(app.mutex);
            if (app.capture_continuous_cancel) {
                return false;
            }
        }
        const int step_ms = std::min(50, total_ms - elapsed_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
        elapsed_ms += step_ms;
    }
    return true;
}

bool open_configured_session(const AppSnapshot &snapshot,
                             dfone::DfOneSession &session,
                             bool *record_has_ssd,
                             std::string &error)
{
    dfone::DfOneSession next_session;
    if (!next_session.connect(make_endpoint(snapshot))) {
        error = next_session.last_error();
        return false;
    }
    if (!next_session.configure(make_config(snapshot))) {
        error = next_session.last_error();
        next_session.disconnect();
        return false;
    }
    bool has_ssd = false;
    if (!next_session.get_record_storage_has_ssd(has_ssd)) {
        error = next_session.last_error();
        next_session.disconnect();
        return false;
    }

    session = std::move(next_session);
    if (record_has_ssd) {
        *record_has_ssd = has_ssd;
    }
    return true;
}

void mark_disconnected_if_session_closed_locked(AppState &app)
{
    if (!app.session.connected()) {
        app.session.disconnect();
        app.connected = false;
        app.pending_control_update = {};
    }
}

bool app_session_is_connected(AppState &app)
{
    std::lock_guard<std::mutex> lock(app.mutex);
    return app.connected && app.session.connected();
}

bool reconnect_app_session(AppState &app,
                           const AppSnapshot &snapshot,
                           std::string &error)
{
    {
        std::lock_guard<std::mutex> lock(app.mutex);
        app.session.disconnect();
        app.connected = false;
        app.pending_control_update = {};
    }

    bool record_has_ssd = false;
    dfone::DfOneSession session;
    if (!open_configured_session(snapshot, session, &record_has_ssd, error)) {
        std::lock_guard<std::mutex> lock(app.mutex);
        app.connected = false;
        app.pending_control_update = {};
        return false;
    }

    std::lock_guard<std::mutex> lock(app.mutex);
    app.session = std::move(session);
    app.connected = true;
    app.record_has_ssd = record_has_ssd;
    app.pending_control_update = {};
    return true;
}

template <typename CancelledFn, typename StatusFn>
bool reconnect_until_ready(AppState &app,
                           const AppSnapshot &snapshot,
                           CancelledFn cancelled,
                           StatusFn set_status,
                           std::string &error)
{
    std::string reconnect_error;
    while (true) {
        if (cancelled()) {
            error.clear();
            return false;
        }
        set_status(reconnect_error);
        if (reconnect_app_session(app, snapshot, reconnect_error)) {
            error.clear();
            return true;
        }
        for (int elapsed_ms = 0; elapsed_ms < 1000; elapsed_ms += 100) {
            if (cancelled()) {
                error.clear();
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

#ifdef DFONE_USER_ENABLE_DOA
bool sleep_with_doa_cancel(AppState &app, int interval_ms)
{
    const int total_ms = std::max(0, interval_ms);
    int elapsed_ms = 0;
    while (elapsed_ms < total_ms) {
        {
            std::lock_guard<std::mutex> lock(app.mutex);
            if (app.doa_cancel) {
                return false;
            }
        }
        const int step_ms = std::min(50, total_ms - elapsed_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
        elapsed_ms += step_ms;
    }
    return true;
}

bool capture_and_estimate_doa(AppState &app,
                              const AppSnapshot &snapshot,
                              std::string &error,
                              bool stop_on_zc1_miss)
{
    dfone::DfOneIqCapture capture;
    const std::size_t frames = static_cast<std::size_t>(std::max(1, snapshot.frames));
    if (!app.session.capture_iq(frames, capture)) {
        error = app.session.last_error();
        return false;
    }

    dfone::DoaConfig config;
    Zc1DetectorConfig zc1_config;
    {
        std::lock_guard<std::mutex> lock(app.mutex);
        if (app.doa_follow_rx_lo) {
            app.doa_center_freq_mhz = capture.rx_lo_hz > 0
                                          ? static_cast<double>(capture.rx_lo_hz) / 1.0e6
                                          : snapshot.rx_lo_mhz;
        }
        config = make_doa_config(app, capture);
        zc1_config = make_zc1_detector_config(app);
    }

    dfone::DfOneIqCapture doa_capture = capture;
    Zc1DetectionResult zc1_result;
    if (zc1_config.enabled) {
        zc1_result = detect_zc1_in_capture(capture, zc1_config);
        if (!zc1_result.detected) {
            error = zc1_result.error + " (best score=" +
                    std::to_string(zc1_result.score) + ")";
            if (!stop_on_zc1_miss) {
                return true;
            }
            std::lock_guard<std::mutex> lock(app.mutex);
            app.doa_zc1_last_detected = false;
            app.doa_zc1_last_start = zc1_result.start_sample;
            app.doa_zc1_last_score = zc1_result.score;
            app.doa_error = error;
            return false;
        }

        const std::size_t requested_count =
            zc1_config.capture_samples == 0
                ? zc1_result.symbol_samples
                : zc1_config.capture_samples;
        doa_capture = slice_capture_for_doa(capture,
                                            zc1_result.start_sample,
                                            requested_count);
        config.start_sample = 0;
        if (config.sample_count == 0 ||
            config.sample_count > doa_capture.frames) {
            config.sample_count = doa_capture.frames;
        }
    }

    const dfone::DoaResult result = estimate_doa_for_capture(doa_capture, config);

    {
        std::lock_guard<std::mutex> lock(app.mutex);
        app.doa_capture = std::move(doa_capture);
        update_doa_preview_locked(app, app.doa_capture);
        app.doa_zc1_last_detected = zc1_config.enabled ? zc1_result.detected : false;
        app.doa_zc1_last_start = zc1_result.start_sample;
        app.doa_zc1_last_score = zc1_result.score;
        app.doa_result = result;
        app.doa_error = result.valid ? std::string{} : result.error;
    }

    if (!result.valid) {
        error = result.error;
        return false;
    }
    return true;
}
#endif

void store_capture(AppState &app, dfone::DfOneIqCapture capture)
{
    {
        std::lock_guard<std::mutex> lock(app.mutex);
        app.last_capture = std::move(capture);
    }
    update_preview(app);
}

void connect_device(AppState &app, const AppSnapshot &snapshot)
{
    start_worker(app, "Connecting", [snapshot, &app](std::string &error) {
        bool record_has_ssd = false;
        dfone::DfOneSession session;
        if (!open_configured_session(snapshot, session, &record_has_ssd, error)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(app.mutex);
        app.session.disconnect();
        app.session = std::move(session);
        app.connected = true;
        app.record_has_ssd = record_has_ssd;
        app.pending_control_update = {};
        app.capture_continuous_iterations = 0;
        return true;
    });
}

void disconnect_device(AppState &app)
{
    std::lock_guard<std::mutex> lock(app.mutex);
    if (app.busy) {
        return;
    }
    app.session.disconnect();
    app.connected = false;
    app.status = "Disconnected";
    app.error.clear();
    app.pending_control_update = {};
}

void capture_iq(AppState &app, const AppSnapshot &snapshot, bool uncorrected)
{
    start_worker(app,
                 uncorrected ? "Capturing uncorrected IQ" : "Capturing calibrated IQ",
                 [snapshot, uncorrected, &app](std::string &error) {
        {
            std::lock_guard<std::mutex> lock(app.mutex);
            if (!app.connected) {
                error = "connect first";
                return false;
            }
        }

        dfone::DfOneIqCapture capture;
        const std::size_t frames = static_cast<std::size_t>(std::max(1, snapshot.frames));
        const bool ok = uncorrected
                            ? app.session.capture_uncorrected_iq(frames, capture)
                            : app.session.capture_iq(frames, capture);
        if (!ok) {
            error = app.session.last_error();
            std::lock_guard<std::mutex> lock(app.mutex);
            mark_disconnected_if_session_closed_locked(app);
            return false;
        }

        if (snapshot.save_to_file &&
            !write_capture_file(snapshot.output_path.c_str(), capture.payload)) {
            error = "write output failed";
            return false;
        }

        store_capture(app, std::move(capture));
        return true;
    });
}

void save_baseband_iq(AppState &app, const AppSnapshot &snapshot)
{
    start_worker(app, "Saving 8CH baseband IQ", [snapshot, &app](std::string &error) {
        const std::uint64_t total_bytes =
            static_cast<std::uint64_t>(std::max(1, snapshot.baseband_save_length_mb)) *
            1024ULL * 1024ULL;
        {
            std::lock_guard<std::mutex> lock(app.mutex);
            if (!app.connected) {
                error = "connect first";
                return false;
            }
            app.baseband_save_running = true;
            app.baseband_save_cancel = false;
            app.baseband_save_progress = 0.0;
            app.baseband_save_written_bytes = 0;
            app.baseband_save_total_bytes = total_bytes;
            app.baseband_save_stage = "Opening baseband output";
        }

        std::ofstream out(snapshot.baseband_save_output_path,
                          std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "open baseband output failed";
            std::lock_guard<std::mutex> lock(app.mutex);
            app.baseband_save_running = false;
            app.baseband_save_stage = error;
            return false;
        }

        if (!app.session.configure(make_config(snapshot))) {
            error = app.session.last_error();
            std::lock_guard<std::mutex> lock(app.mutex);
            mark_disconnected_if_session_closed_locked(app);
            app.baseband_save_running = false;
            app.baseband_save_stage = error;
            return false;
        }

        bool ok = true;
        std::uint64_t written = 0;
        while (written < total_bytes) {
            {
                std::lock_guard<std::mutex> lock(app.mutex);
                if (app.baseband_save_cancel) {
                    ok = false;
                    break;
                }
                app.baseband_save_stage = "Capturing 8CH baseband IQ";
            }

            const std::uint64_t remaining_bytes = total_bytes - written;
            const std::uint64_t remaining_frames =
                (remaining_bytes + dfone::kDfOneApiBytesPerFrame - 1) /
                dfone::kDfOneApiBytesPerFrame;
            const std::size_t frames =
                static_cast<std::size_t>(std::max<std::uint64_t>(
                    1,
                    std::min<std::uint64_t>(remaining_frames,
                                            kBasebandSaveMaxChunkFrames)));

            AppState::PendingControlUpdate pending_update{};
            if (take_pending_control_update(app, pending_update) &&
                !apply_pending_control_update(app.session, pending_update, error)) {
                std::lock_guard<std::mutex> lock(app.mutex);
                mark_disconnected_if_session_closed_locked(app);
                app.baseband_save_running = false;
                app.baseband_save_stage = error;
                return false;
            }

            dfone::DfOneIqCapture capture;
            if (!app.session.capture_iq(frames, capture)) {
                error = app.session.last_error();
                std::lock_guard<std::mutex> lock(app.mutex);
                mark_disconnected_if_session_closed_locked(app);
                app.baseband_save_running = false;
                app.baseband_save_stage = error;
                return false;
            }
            if (capture.payload.empty()) {
                error = "baseband capture returned no data";
                std::lock_guard<std::mutex> lock(app.mutex);
                app.baseband_save_running = false;
                app.baseband_save_stage = error;
                return false;
            }

            const std::size_t bytes_to_write =
                static_cast<std::size_t>(std::min<std::uint64_t>(
                    remaining_bytes,
                    static_cast<std::uint64_t>(capture.payload.size())));
            out.write(reinterpret_cast<const char *>(capture.payload.data()),
                      static_cast<std::streamsize>(bytes_to_write));
            if (!out) {
                error = "write baseband output failed";
                std::lock_guard<std::mutex> lock(app.mutex);
                app.baseband_save_running = false;
                app.baseband_save_stage = error;
                return false;
            }

            written += bytes_to_write;
            {
                std::lock_guard<std::mutex> lock(app.mutex);
                app.baseband_save_written_bytes = written;
                app.baseband_save_progress =
                    total_bytes == 0
                        ? 0.0
                        : static_cast<double>(written) /
                              static_cast<double>(total_bytes);
                app.baseband_save_stage = "Writing 8CH baseband IQ";
            }
            store_capture(app, std::move(capture));
        }

        out.close();
        if (ok && !out) {
            error = "close baseband output failed";
            ok = false;
        }

        std::lock_guard<std::mutex> lock(app.mutex);
        app.baseband_save_running = false;
        app.baseband_save_cancel = false;
        app.baseband_save_written_bytes = written;
        app.baseband_save_progress =
            total_bytes == 0 ? 0.0
                             : static_cast<double>(written) /
                                   static_cast<double>(total_bytes);
        if (ok) {
            app.baseband_save_progress = 1.0;
            app.baseband_save_written_bytes = total_bytes;
            app.baseband_save_stage = "8CH baseband save complete";
        } else if (error.empty()) {
            app.baseband_save_stage = "8CH baseband save stopped";
            return true;
        } else {
            app.baseband_save_stage = error;
        }
        return ok;
    });
}

#ifdef DFONE_USER_ENABLE_DOA
void run_doa_once(AppState &app, const AppSnapshot &snapshot)
{
    start_worker(app, "Running DOA Estimate", [snapshot, &app](std::string &error) {
        {
            std::lock_guard<std::mutex> lock(app.mutex);
            if (!app.connected) {
                error = "connect first";
                return false;
            }
            app.doa_running = true;
            app.doa_cancel = false;
            app.doa_error.clear();
        }

        if (!app.session.configure(make_config(snapshot))) {
            error = app.session.last_error();
            std::lock_guard<std::mutex> lock(app.mutex);
            mark_disconnected_if_session_closed_locked(app);
            app.doa_running = false;
            app.doa_error = error;
            return false;
        }

        const bool ok = capture_and_estimate_doa(app, snapshot, error, true);

        std::lock_guard<std::mutex> lock(app.mutex);
        if (!ok) {
            mark_disconnected_if_session_closed_locked(app);
        }
        app.doa_running = false;
        app.doa_cancel = false;
        if (!ok && app.doa_error.empty()) {
            app.doa_error = error;
        }
        return ok;
    });
}

void start_continuous_doa(AppState &app, const AppSnapshot &snapshot)
{
    start_worker(app, "Continuous DOA Estimation", [snapshot, &app](std::string &error) {
        {
            std::lock_guard<std::mutex> lock(app.mutex);
            if (!app.connected) {
                error = "connect first";
                return false;
            }
            app.doa_running = true;
            app.doa_cancel = false;
            app.doa_iterations = 0;
            app.doa_error.clear();
        }

        if (!app.session.configure(make_config(snapshot))) {
            error = app.session.last_error();
            if (!app_session_is_connected(app)) {
                const bool reconnected = reconnect_until_ready(
                    app,
                    snapshot,
                    [&app]() {
                        std::lock_guard<std::mutex> lock(app.mutex);
                        return app.doa_cancel;
                    },
                    [&app](const std::string &reconnect_error) {
                        std::lock_guard<std::mutex> lock(app.mutex);
                        app.doa_error = reconnect_error.empty()
                                            ? "connection lost; reconnecting"
                                            : "connection lost; reconnecting: " + reconnect_error;
                    },
                    error);
                if (!reconnected && error.empty()) {
                    std::lock_guard<std::mutex> lock(app.mutex);
                    app.doa_running = false;
                    app.doa_cancel = false;
                    return true;
                }
            }
            if (!error.empty()) {
                std::lock_guard<std::mutex> lock(app.mutex);
                mark_disconnected_if_session_closed_locked(app);
                app.doa_running = false;
                app.doa_error = error;
                return false;
            }
        }

        bool ok = true;
        while (true) {
            {
                std::lock_guard<std::mutex> lock(app.mutex);
                if (app.doa_cancel) {
                    break;
                }
            }

            AppState::PendingControlUpdate pending_update{};
            if (take_pending_control_update(app, pending_update) &&
                !apply_pending_control_update(app.session, pending_update, error)) {
                if (!app_session_is_connected(app)) {
                    const bool reconnected = reconnect_until_ready(
                        app,
                        snapshot,
                        [&app]() {
                            std::lock_guard<std::mutex> lock(app.mutex);
                            return app.doa_cancel;
                        },
                        [&app](const std::string &reconnect_error) {
                            std::lock_guard<std::mutex> lock(app.mutex);
                            app.doa_error = reconnect_error.empty()
                                                ? "connection lost; reconnecting"
                                                : "connection lost; reconnecting: " + reconnect_error;
                        },
                        error);
                    if (reconnected) {
                        continue;
                    }
                    if (error.empty()) {
                        break;
                    }
                }
                ok = false;
                break;
            }

            if (!capture_and_estimate_doa(app, snapshot, error, false)) {
                if (!app_session_is_connected(app)) {
                    const bool reconnected = reconnect_until_ready(
                        app,
                        snapshot,
                        [&app]() {
                            std::lock_guard<std::mutex> lock(app.mutex);
                            return app.doa_cancel;
                        },
                        [&app](const std::string &reconnect_error) {
                            std::lock_guard<std::mutex> lock(app.mutex);
                            app.doa_error = reconnect_error.empty()
                                                ? "connection lost; reconnecting"
                                                : "connection lost; reconnecting: " + reconnect_error;
                        },
                        error);
                    if (reconnected) {
                        continue;
                    }
                    if (error.empty()) {
                        break;
                    }
                }
                ok = false;
                break;
            }

            {
                std::lock_guard<std::mutex> lock(app.mutex);
                ++app.doa_iterations;
            }

            if (!sleep_with_doa_cancel(app, snapshot.capture_continuous_interval_ms)) {
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(app.mutex);
            app.doa_running = false;
            app.doa_cancel = false;
            if (!ok && app.doa_error.empty()) {
                app.doa_error = error;
            }
        }
        return ok;
    });
}
#endif

void record_iq(AppState &app, const AppSnapshot &snapshot)
{
    start_worker(app, "Recording IQ", [snapshot, &app](std::string &error) {
        {
            std::lock_guard<std::mutex> lock(app.mutex);
            if (!app.connected) {
                error = "connect first";
                return false;
            }
            app.record_running = true;
            app.record_progress = 0.0;
            app.record_written_bytes = 0;
            app.record_total_bytes =
                static_cast<std::uint64_t>(std::max(0, snapshot.record_length_mb)) *
                1024ULL * 1024ULL;
            app.record_stage = "Starting IQ record";
        }

        if (!app.session.configure(make_config(snapshot))) {
            error = app.session.last_error();
            std::lock_guard<std::mutex> lock(app.mutex);
            app.record_running = false;
            app.record_stage = error;
            return false;
        }

        const auto config = make_record_config(snapshot);
        if (!app.session.record_iq(
                config,
                [&app](const dfone::DfOneRecordProgress &progress) {
                    std::lock_guard<std::mutex> lock(app.mutex);
                    app.record_progress = progress.fraction;
                    app.record_written_bytes = progress.bytes_written;
                    app.record_total_bytes = progress.total_bytes;
                    app.record_stage = "Recording IQ";
                })) {
            error = app.session.last_error();
            std::lock_guard<std::mutex> lock(app.mutex);
            app.record_running = false;
            app.record_stage = error;
            return false;
        }

        std::lock_guard<std::mutex> lock(app.mutex);
        app.record_running = false;
        app.record_progress = 1.0;
        app.record_written_bytes = app.record_total_bytes;
        app.record_stage = "IQ record complete";
        return true;
    });
}

void refresh_record_storage_status(AppState &app)
{
    start_worker(app, "Reading FPGA SSD status", [&app](std::string &error) {
        bool record_has_ssd = false;
        if (!app.session.get_record_storage_has_ssd(record_has_ssd)) {
            error = app.session.last_error();
            return false;
        }

        std::lock_guard<std::mutex> lock(app.mutex);
        app.record_has_ssd = record_has_ssd;
        return true;
    });
}

void set_reference_clock(AppState &app, dfone::DfOneReferenceClock value)
{
    start_worker(app, "Setting reference clock", [value, &app](std::string &error) {
        if (!app.session.set_reference_clock(value)) {
            error = app.session.last_error();
            return false;
        }
        return true;
    });
}

void set_sample_rate(AppState &app, std::uint32_t value)
{
    start_worker(app, "Setting sample rate", [value, &app](std::string &error) {
        if (!app.session.set_sample_rate(value)) {
            error = app.session.last_error();
            return false;
        }
        return true;
    });
}

void set_rx_lo(AppState &app, std::uint64_t value)
{
    start_worker(app, "Setting RX LO", [value, &app](std::string &error) {
        if (!app.session.set_rx_lo(value)) {
            error = app.session.last_error();
            return false;
        }
        return true;
    });
}

void set_rx_gain(AppState &app, std::uint32_t value)
{
    start_worker(app, "Setting RX gain", [value, &app](std::string &error) {
        if (!app.session.set_rx_gain(value)) {
            error = app.session.last_error();
            return false;
        }
        return true;
    });
}

void update_firmware(AppState &app,
                     const AppSnapshot &snapshot,
                     dfone::DfOneFirmwareTarget target)
{
    const bool qspi = target == dfone::DfOneFirmwareTarget::kQspi;
    start_worker(app,
                 qspi ? "Updating QSPI firmware" : "Updating eMMC firmware",
                 [snapshot, target, qspi, &app](std::string &error) {
        {
            std::lock_guard<std::mutex> lock(app.mutex);
            app.firmware_update_running = true;
            app.firmware_update_progress = 0.0;
            app.firmware_update_stage =
                qspi ? "Starting QSPI firmware update" : "Starting eMMC firmware update";
            app.maintenance_output.clear();
        }

        dfone::DfOneFirmwareUpdateConfig config;
        config.target = target;
        config.package_path = qspi ? snapshot.qspi_firmware_path
                                   : snapshot.emmc_firmware_path;
        config.reboot_after = snapshot.firmware_update_reboot_after;

        dfone::DfOneMaintenance maintenance(make_maintenance_endpoint(snapshot));
        std::string output;
        const bool ok = maintenance.update_firmware(
            config,
            [&app](const dfone::DfOneFirmwareProgress &progress) {
                std::lock_guard<std::mutex> lock(app.mutex);
                app.firmware_update_progress = progress.fraction;
                app.firmware_update_stage = progress.stage;
                if (!progress.output.empty()) {
                    app.maintenance_output = progress.output;
                }
            },
            output);

        std::lock_guard<std::mutex> lock(app.mutex);
        app.firmware_update_running = false;
        if (!ok) {
            error = maintenance.last_error();
            app.maintenance_output = output.empty() ? error : output;
            return false;
        }
        app.firmware_update_progress = 1.0;
        app.firmware_update_stage =
            qspi ? "QSPI firmware update complete" : "eMMC firmware update complete";
        app.maintenance_output = output;
        return true;
    });
}

void start_continuous_capture(AppState &app, const AppSnapshot &snapshot)
{
    start_worker(app, "Continuous IQ Capture", [snapshot, &app](std::string &error) {
        {
            std::lock_guard<std::mutex> lock(app.mutex);
            if (!app.connected) {
                error = "connect first";
                return false;
            }
            app.capture_continuous_running = true;
            app.capture_continuous_cancel = false;
            app.capture_continuous_iterations = 0;
        }

        bool ok = true;
        while (true) {
            {
                std::lock_guard<std::mutex> lock(app.mutex);
                if (app.capture_continuous_cancel) {
                    break;
                }
            }

            AppState::PendingControlUpdate pending_update{};
            if (take_pending_control_update(app, pending_update) &&
                !apply_pending_control_update(app.session, pending_update, error)) {
                if (!app_session_is_connected(app)) {
                    const bool reconnected = reconnect_until_ready(
                        app,
                        snapshot,
                        [&app]() {
                            std::lock_guard<std::mutex> lock(app.mutex);
                            return app.capture_continuous_cancel;
                        },
                        [&app](const std::string &reconnect_error) {
                            std::lock_guard<std::mutex> lock(app.mutex);
                            app.error = reconnect_error.empty()
                                            ? "connection lost; reconnecting"
                                            : "connection lost; reconnecting: " + reconnect_error;
                        },
                        error);
                    if (reconnected) {
                        continue;
                    }
                    if (error.empty()) {
                        break;
                    }
                }
                ok = false;
                break;
            }

            dfone::DfOneIqCapture capture;
            const std::size_t frames =
                static_cast<std::size_t>(std::max(1, snapshot.frames));
            if (!app.session.capture_iq(frames, capture)) {
                error = app.session.last_error();
                if (!app_session_is_connected(app)) {
                    const bool reconnected = reconnect_until_ready(
                        app,
                        snapshot,
                        [&app]() {
                            std::lock_guard<std::mutex> lock(app.mutex);
                            return app.capture_continuous_cancel;
                        },
                        [&app](const std::string &reconnect_error) {
                            std::lock_guard<std::mutex> lock(app.mutex);
                            app.error = reconnect_error.empty()
                                            ? "connection lost; reconnecting"
                                            : "connection lost; reconnecting: " + reconnect_error;
                        },
                        error);
                    if (reconnected) {
                        continue;
                    }
                    if (error.empty()) {
                        break;
                    }
                }
                ok = false;
                break;
            }

            store_capture(app, std::move(capture));
            {
                std::lock_guard<std::mutex> lock(app.mutex);
                ++app.capture_continuous_iterations;
            }

            if (!sleep_with_capture_cancel(app, snapshot.capture_continuous_interval_ms)) {
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(app.mutex);
            if (!ok) {
                mark_disconnected_if_session_closed_locked(app);
            }
            app.capture_continuous_running = false;
            app.capture_continuous_cancel = false;
        }
        return ok;
    });
}

void join_worker_if_idle(AppState &app)
{
    std::lock_guard<std::mutex> lock(app.mutex);
    if (!app.busy && app.worker.joinable()) {
        app.worker.join();
    }
}

void shutdown_app(AppState &app)
{
    {
        std::lock_guard<std::mutex> lock(app.mutex);
        app.capture_continuous_cancel = true;
        app.baseband_save_cancel = true;
#ifdef DFONE_USER_ENABLE_DOA
        app.doa_cancel = true;
#endif
    }
    if (app.worker.joinable()) {
        app.worker.join();
    }
    {
        std::lock_guard<std::mutex> lock(app.mutex);
        app.session.disconnect();
        app.connected = false;
    }
}

#ifdef DFONE_USER_ENABLE_DOA
void draw_doa_panel(AppState &app, const AppUiSnapshot &ui)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("DOA Estimation", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    if (app.doa_follow_rx_lo) {
        app.doa_center_freq_mhz =
            ui.rx_lo_hz > 0
                ? static_cast<double>(ui.rx_lo_hz) / 1.0e6
                : app.rx_lo_mhz;
    }
    ImGui::Checkbox("Follow Main RX LO", &app.doa_follow_rx_lo);
    ImGui::SameLine();
    ImGui::TextDisabled("center=%.6f MHz", app.doa_center_freq_mhz);
    ImGui::BeginDisabled(app.doa_follow_rx_lo);
    input_double_commit("DOA Center MHz", &app.doa_center_freq_mhz);
    if (app.doa_center_freq_mhz < 0.000001) {
        app.doa_center_freq_mhz = 0.000001;
    }
    ImGui::EndDisabled();

    ImGui::Combo("DOA Algorithm",
                 &app.doa_algorithm,
                 "Conventional ULA\0Conventional Circular\0MUSIC Circular\0Capon Circular\0");
    app.doa_algorithm = std::max(0, std::min(3, app.doa_algorithm));
    if (app.doa_algorithm == 0) {
        input_double_commit("Element Spacing m", &app.doa_element_spacing_m);
        app.doa_element_spacing_m = std::max(0.000001, app.doa_element_spacing_m);
    } else {
        input_double_commit("Array Radius m", &app.doa_array_radius_m);
        app.doa_array_radius_m = std::max(0.000001, app.doa_array_radius_m);
    }
    input_double_commit("Angle Min deg", &app.doa_angle_min_deg);
    input_double_commit("Angle Max deg", &app.doa_angle_max_deg);
    if (app.doa_angle_max_deg <= app.doa_angle_min_deg) {
        app.doa_angle_max_deg = app.doa_angle_min_deg + 1.0;
    }
    input_double_commit("Angle Step deg", &app.doa_angle_step_deg);
    app.doa_angle_step_deg = std::max(0.01, app.doa_angle_step_deg);
    input_int_commit("DOA Start Sample", &app.doa_start_sample);
    app.doa_start_sample = std::max(0, app.doa_start_sample);
    input_int_commit("DOA Sample Count", &app.doa_sample_count);
    app.doa_sample_count = std::max(0, app.doa_sample_count);
    ImGui::SeparatorText("DJI ZC1 Gate");
    ImGui::Checkbox("Detect ZC1 Before DOA", &app.doa_zc1_detection_enabled);
    ImGui::BeginDisabled(!app.doa_zc1_detection_enabled);
    input_int_commit("ZC1 Channel", &app.doa_zc1_channel);
    app.doa_zc1_channel =
        std::max(0, std::min<int>(static_cast<int>(kChannelCount) - 1,
                                  app.doa_zc1_channel));
    input_int_commit("ZC1 FFT Size", &app.doa_zc1_fft_size);
    app.doa_zc1_fft_size = std::max(16, app.doa_zc1_fft_size);
    input_int_commit("ZC1 Carriers", &app.doa_zc1_carriers);
    app.doa_zc1_carriers =
        std::max(1, std::min(app.doa_zc1_fft_size - 1, app.doa_zc1_carriers));
    input_int_commit("ZC1 CP Length", &app.doa_zc1_cp_len);
    app.doa_zc1_cp_len = std::max(0, std::min(app.doa_zc1_fft_size, app.doa_zc1_cp_len));
    input_int_commit("ZC1 Root", &app.doa_zc1_root);
    app.doa_zc1_root = std::max(1, app.doa_zc1_root);
    input_int_commit("ZC1 Decimation", &app.doa_zc1_decimation);
    app.doa_zc1_decimation = std::max(1, app.doa_zc1_decimation);
    input_double_commit("ZC1 Threshold", &app.doa_zc1_threshold);
    app.doa_zc1_threshold = std::max(0.0, app.doa_zc1_threshold);
    input_int_commit("ZC1 Search Stride", &app.doa_zc1_search_stride);
    app.doa_zc1_search_stride = std::max(1, app.doa_zc1_search_stride);
    input_int_commit("ZC1 Max Search Samples", &app.doa_zc1_max_search_samples);
    app.doa_zc1_max_search_samples = std::max(0, app.doa_zc1_max_search_samples);
    input_int_commit("ZC1 DOA Samples", &app.doa_zc1_capture_samples);
    app.doa_zc1_capture_samples = std::max(0, app.doa_zc1_capture_samples);
    ImGui::TextDisabled("DJI defaults: sample rate 15.36 MSPS, bandwidth 10 MHz");
    if (ui.doa_zc1_score > 0.0) {
        ImGui::TextDisabled("last ZC1 %s  start=%zu  score=%.4f",
                            ui.doa_zc1_detected ? "hit" : "miss",
                            ui.doa_zc1_start,
                            ui.doa_zc1_score);
    }
    ImGui::EndDisabled();
    input_int_commit("Peak Count", &app.doa_peak_count);
    app.doa_peak_count = std::max(1, std::min(8, app.doa_peak_count));
    input_int_commit("Signal Count", &app.doa_signal_count);
    app.doa_signal_count = std::max(1, std::min(7, app.doa_signal_count));
    ImGui::Checkbox("Remove DC", &app.doa_remove_dc);
    ImGui::SameLine();
    ImGui::Checkbox("Normalize Channels", &app.doa_normalize_channels);

    ImGui::TextDisabled("DOA Channels:");
    ImGui::SameLine();
    for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
        ImGui::PushID(static_cast<int>(ch));
        ImGui::Checkbox((std::string("CH") + std::to_string(ch)).c_str(),
                        &app.doa_enabled_channels[ch]);
        ImGui::PopID();
        if (ch + 1 < kChannelCount) {
            ImGui::SameLine();
        }
    }

    const bool can_start_doa = ui.connected && !ui.busy && !ui.capture_continuous_running &&
                               !ui.record_running && !ui.baseband_save_running;

    if (ui.doa_running) {
        if (ImGui::Button("Stop DOA", ImVec2(180, 0))) {
            std::lock_guard<std::mutex> lock(app.mutex);
            app.doa_cancel = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("DOA running, updates=%llu",
                            static_cast<unsigned long long>(ui.doa_iterations));
    } else {
        ImGui::BeginDisabled(!can_start_doa);
        if (ImGui::Button("Run DOA Once", ImVec2(180, 0))) {
            run_doa_once(app, snapshot_app(app));
        }
        ImGui::SameLine();
        if (ImGui::Button("Start Continuous DOA", ImVec2(190, 0))) {
            start_continuous_doa(app, snapshot_app(app));
        }
        ImGui::EndDisabled();
    }
    if (!ui.doa_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.28f, 1.0f),
                           "%s",
                           ui.doa_error.c_str());
    }
    if (ui.doa_result.valid) {
        const double peak_angle =
            ui.doa_result.peaks.empty() ? 0.0 : ui.doa_result.peaks.front().angle_deg;
        ImGui::Text("DOA peak=%.2f deg  confidence=%s  SNR=%.1f dB",
                    peak_angle,
                    ui.doa_result.confidence.c_str(),
                    ui.doa_result.estimated_snr_db);
        ImGui::TextDisabled("center=%.6f MHz  samples=%zu  spacing=%.3f lambda",
                            ui.doa_result.center_freq_hz / 1.0e6,
                            ui.doa_result.sample_count,
                            ui.doa_result.spacing_wavelengths);
        if (!ui.doa_result.warning.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.74f, 0.30f, 1.0f),
                               "%s",
                               ui.doa_result.warning.c_str());
        }
    }

    if (ui.doa_preview_count > 1) {
        draw_multichannel_plot("DOA I waveform",
                               ui.doa_preview_i,
                               ui.doa_preview_count,
                               -4096.0f,
                               4096.0f,
                               150.0f);
    } else {
        ImGui::TextDisabled("Run DOA to show the DOA I waveform");
    }
    draw_doa_spectrum_plot("DOA spectrum", ui.doa_result);
}
#endif

}  // namespace

int main()
{
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(1120, 760, "DFONE Public API Example", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "glewInit failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    apply_style();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    AppState app;
    FilePickerState emmc_firmware_picker;
    FilePickerState qspi_firmware_picker;
    while (!glfwWindowShouldClose(window)) {
        join_worker_if_idle(app);
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const AppUiSnapshot ui = snapshot_ui(app);
#ifdef DFONE_USER_ENABLE_DOA
        const bool live_acquisition_running =
            ui.capture_continuous_running || ui.doa_running || ui.baseband_save_running;
#else
        const bool live_acquisition_running =
            ui.capture_continuous_running || ui.baseband_save_running;
#endif
        const bool iq_capture_busy = live_acquisition_running || ui.record_running;
        const bool can_capture_iq = ui.connected && !ui.busy && !iq_capture_busy;
        const bool can_start_continuous_iq = ui.connected && !ui.busy && !ui.record_running;
        const bool can_use_record_controls =
            ui.connected && !ui.busy && !live_acquisition_running;
        const bool can_start_record = can_use_record_controls && !ui.record_running;

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("DFONE Public API Example", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImGui::Text("DFONE Public API Example");
        ImGui::Separator();
        ImGui::Text("Connection: %s", ui.connected ? "connected" : "disconnected");
        ImGui::SameLine();
        ImGui::Text("State: %s%s", ui.status.c_str(), ui.busy ? "..." : "");
        if (!ui.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.28f, 1.0f), "%s", ui.error.c_str());
        }
        ImGui::Separator();

        static float settings_width = 540.0f;
        constexpr float kSplitterWidth = 7.0f;
        constexpr float kMinSettingsWidth = 420.0f;
        constexpr float kMinCaptureWidth = 380.0f;
        const ImVec2 main_avail = ImGui::GetContentRegionAvail();
        const float max_settings_width =
            std::max(kMinSettingsWidth, main_avail.x - kSplitterWidth - kMinCaptureWidth);
        settings_width = std::max(kMinSettingsWidth,
                                  std::min(settings_width, max_settings_width));

        ImGui::BeginChild("settings", ImVec2(settings_width, 0), true);
        ImGui::Text("Device");
        ImGui::InputText("Device IP", app.device_ip, sizeof(app.device_ip));
        ImGui::InputInt("Command Port", &app.command_port);
        ImGui::InputInt("Data Port", &app.data_port);
        app.command_port = std::max(1, std::min(65535, app.command_port));
        app.data_port = std::max(1, std::min(65535, app.data_port));

        if (ImGui::Button(ui.connected ? "Reconnect" : "Connect", ImVec2(140, 0)) &&
            !ui.busy) {
            connect_device(app, snapshot_app(app));
        }
        ImGui::SameLine();
        if (ImGui::Button("Disconnect", ImVec2(140, 0)) && !ui.busy) {
            disconnect_device(app);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Synchronized Capture");
        if (ImGui::Combo("Reference Clock", &app.reference_clock,
                         "Default\0External\0Source 2\0Source 3\0")) {
            app.reference_clock = std::max(0, std::min(3, app.reference_clock));
            const auto value = static_cast<dfone::DfOneReferenceClock>(app.reference_clock);
            if (live_acquisition_running && ui.connected) {
                std::lock_guard<std::mutex> lock(app.mutex);
                app.pending_control_update.has_reference_clock = true;
                app.pending_control_update.reference_clock = value;
            } else if (!ui.busy && ui.connected) {
                set_reference_clock(app, value);
            }
        }
        if (input_double_commit("Sample Rate MSPS", &app.sample_rate_msps)) {
            if (app.sample_rate_msps < 0.000001) {
                app.sample_rate_msps = 0.000001;
            }
            const std::uint32_t value = sample_rate_hz_from_msps(app.sample_rate_msps);
            if (live_acquisition_running && ui.connected) {
                std::lock_guard<std::mutex> lock(app.mutex);
                app.pending_control_update.has_sample_rate_hz = true;
                app.pending_control_update.sample_rate_hz = value;
            } else if (!ui.busy && ui.connected) {
                set_sample_rate(app, value);
            }
        }
        if (input_double_commit("RX LO MHz", &app.rx_lo_mhz)) {
            if (app.rx_lo_mhz < 0.0) {
                app.rx_lo_mhz = 0.0;
            }
            const std::uint64_t value = rx_lo_hz_from_mhz(app.rx_lo_mhz);
            if (live_acquisition_running && ui.connected) {
                std::lock_guard<std::mutex> lock(app.mutex);
                app.pending_control_update.has_rx_lo_hz = true;
                app.pending_control_update.rx_lo_hz = value;
            } else if (!ui.busy && ui.connected) {
                set_rx_lo(app, value);
            }
        }
        if (input_int_commit("RX Gain dB", &app.rx_gain_db)) {
            app.rx_gain_db = std::max(0, std::min(76, app.rx_gain_db));
            const std::uint32_t value = static_cast<std::uint32_t>(app.rx_gain_db);
            if (live_acquisition_running && ui.connected) {
                std::lock_guard<std::mutex> lock(app.mutex);
                app.pending_control_update.has_rx_gain_db = true;
                app.pending_control_update.rx_gain_db = value;
            } else if (!ui.busy && ui.connected) {
                set_rx_gain(app, value);
            }
        }
        ImGui::InputInt("Frames", &app.frames);
        app.frames = std::max(1, app.frames);
        ImGui::InputInt("Continuous Interval ms", &app.capture_continuous_interval_ms);
        app.capture_continuous_interval_ms = std::max(0, app.capture_continuous_interval_ms);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Checkbox("Save app-side CS16", &app.save_to_file);
        ImGui::InputText("Output Path", app.output_path, sizeof(app.output_path));
        ImGui::BeginDisabled(!can_capture_iq);
        if (ImGui::Button("Capture Calibrated IQ", ImVec2(210, 0))) {
            capture_iq(app, snapshot_app(app), false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Capture Uncorrected IQ", ImVec2(210, 0))) {
            capture_iq(app, snapshot_app(app), true);
        }
        ImGui::EndDisabled();
        if (ui.capture_continuous_running) {
            if (ImGui::Button("Stop Continuous IQ", ImVec2(210, 0))) {
                std::lock_guard<std::mutex> lock(app.mutex);
                app.capture_continuous_cancel = true;
            }
            ImGui::TextDisabled("Continuous IQ running, updates=%llu",
                                static_cast<unsigned long long>(
                                    ui.capture_continuous_iterations));
        } else {
            ImGui::BeginDisabled(!can_start_continuous_iq);
            if (ImGui::Button("Start Continuous IQ", ImVec2(210, 0))) {
                start_continuous_capture(app, snapshot_app(app));
            }
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("8CH Baseband Save");
        ImGui::BeginDisabled(live_acquisition_running || ui.record_running || ui.busy);
        ImGui::InputInt("Baseband Length MB", &app.baseband_save_length_mb);
        app.baseband_save_length_mb =
            std::max(1, std::min(1024 * 1024, app.baseband_save_length_mb));
        ImGui::InputText("Baseband Output",
                         app.baseband_save_output_path,
                         sizeof(app.baseband_save_output_path));
        ImGui::EndDisabled();
        if (ui.baseband_save_running) {
            if (ImGui::Button("Stop 8CH Baseband Save", ImVec2(210, 0))) {
                std::lock_guard<std::mutex> lock(app.mutex);
                app.baseband_save_cancel = true;
            }
        } else {
            ImGui::BeginDisabled(!can_capture_iq);
            if (ImGui::Button("Save 8CH Baseband", ImVec2(210, 0))) {
                prepare_next_baseband_save_output_path(app);
                save_baseband_iq(app, snapshot_app(app));
            }
            ImGui::EndDisabled();
        }
        if (ui.baseband_save_running || ui.baseband_save_progress > 0.0) {
            const float fraction =
                static_cast<float>(std::max(0.0, std::min(1.0, ui.baseband_save_progress)));
            char overlay[160];
            std::snprintf(overlay,
                          sizeof(overlay),
                          "%.1f%%  %s",
                          ui.baseband_save_progress * 100.0,
                          ui.baseband_save_stage.empty()
                              ? "8CH baseband save"
                              : ui.baseband_save_stage.c_str());
            ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);
            ImGui::TextDisabled("Baseband data: %.2f / %.2f MB",
                                static_cast<double>(ui.baseband_save_written_bytes) /
                                    (1024.0 * 1024.0),
                                static_cast<double>(ui.baseband_save_total_bytes) /
                                    (1024.0 * 1024.0));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("IQ Record");
        ImGui::BeginDisabled(!can_use_record_controls);
        ImGui::Combo("Record Channels", &app.record_channel_index, "1\0 2\0 4\0");
        app.record_channel_index = std::max(0, std::min(2, app.record_channel_index));
        ImGui::EndDisabled();
        ImGui::Text("FPGA SSD Status: %s", app.record_has_ssd ? "present" : "not present");
        ImGui::SameLine();
        ImGui::BeginDisabled(!can_use_record_controls);
        if (ImGui::Button("Refresh SSD", ImVec2(120, 0))) {
            refresh_record_storage_status(app);
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!can_use_record_controls);
        ImGui::InputInt("Record Length MB", &app.record_length_mb);
        const int record_max_mb = app.record_has_ssd ? (1024 * 1024) : 1024;
        app.record_length_mb = std::max(4, std::min(record_max_mb, app.record_length_mb));
        app.record_length_mb = ((app.record_length_mb + 3) / 4) * 4;
        app.record_length_mb = std::min(record_max_mb, app.record_length_mb);
        ImGui::InputText("Record Output", app.record_output_path, sizeof(app.record_output_path));
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!can_start_record);
        if (ImGui::Button("Start IQ Record", ImVec2(210, 0))) {
            prepare_next_record_output_path(app);
            record_iq(app, snapshot_app(app));
        }
        ImGui::EndDisabled();
        if (ui.record_running || ui.record_progress > 0.0) {
            const float fraction =
                static_cast<float>(std::max(0.0, std::min(1.0, ui.record_progress)));
            char overlay[128];
            std::snprintf(overlay,
                          sizeof(overlay),
                          "%.1f%%  %s",
                          ui.record_progress * 100.0,
                          ui.record_stage.empty() ? "IQ record" : ui.record_stage.c_str());
            ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);
            ImGui::TextDisabled("Record data: %.2f / %.2f MB",
                                static_cast<double>(ui.record_written_bytes) /
                                    (1024.0 * 1024.0),
                                static_cast<double>(ui.record_total_bytes) /
                                    (1024.0 * 1024.0));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Firmware");
        ImGui::Checkbox("Reboot After Update", &app.firmware_update_reboot_after);
        draw_path_picker("eMMC .frm",
                         "Select eMMC Firmware Package",
                         app.emmc_firmware_path,
                         sizeof(app.emmc_firmware_path),
                         emmc_firmware_picker);
        draw_path_picker("QSPI .frm",
                         "Select QSPI Firmware Package",
                         app.qspi_firmware_path,
                         sizeof(app.qspi_firmware_path),
                         qspi_firmware_picker);
        if (ImGui::Button("Update eMMC Firmware", ImVec2(210, 0)) && !ui.busy) {
            update_firmware(app, snapshot_app(app), dfone::DfOneFirmwareTarget::kEmmc);
        }
        ImGui::SameLine();
        if (ImGui::Button("Update QSPI Firmware", ImVec2(210, 0)) && !ui.busy) {
            update_firmware(app, snapshot_app(app), dfone::DfOneFirmwareTarget::kQspi);
        }
        if (ui.firmware_update_running || ui.firmware_update_progress > 0.0) {
            const float fraction =
                static_cast<float>(std::max(0.0,
                                            std::min(1.0,
                                                     ui.firmware_update_progress)));
            char overlay[160];
            std::snprintf(overlay,
                          sizeof(overlay),
                          "%.1f%%  %s",
                          ui.firmware_update_progress * 100.0,
                          ui.firmware_update_stage.empty()
                              ? "Firmware update"
                              : ui.firmware_update_stage.c_str());
            ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);
        }
        if (!ui.maintenance_output.empty()) {
            ImGui::TextWrapped("%s", ui.maintenance_output.c_str());
        }
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 0.0f);
        const ImVec2 splitter_pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("main_splitter", ImVec2(kSplitterWidth, main_avail.y));
        if (ImGui::IsItemActive()) {
            settings_width += ImGui::GetIO().MouseDelta.x;
        }
        const ImU32 splitter_color =
            ImGui::IsItemActive() ? IM_COL32(120, 170, 165, 255)
                                  : (ImGui::IsItemHovered() ? IM_COL32(85, 125, 122, 255)
                                                            : IM_COL32(48, 60, 60, 255));
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(splitter_pos.x + 2.0f, splitter_pos.y),
            ImVec2(splitter_pos.x + kSplitterWidth - 2.0f, splitter_pos.y + main_avail.y),
            splitter_color,
            2.0f);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginChild("capture", ImVec2(0, 0), true);
        ImGui::Text("Last Capture");
        if (!ui.has_capture) {
            ImGui::TextDisabled("No IQ captured yet");
        } else {
            ImGui::Text("kind=%s",
                        ui.capture_kind == dfone::DfOneIqKind::kCalibrated
                            ? "calibrated"
                            : "uncorrected");
            ImGui::Text("sample_rate=%u  rx_lo=%llu  frames=%zu  bytes=%zu",
                        ui.sample_rate_hz,
                        static_cast<unsigned long long>(ui.rx_lo_hz),
                        ui.frames,
                        ui.bytes);
            ImGui::TextDisabled("Payload format: CS16 little-endian, CH0_I CH0_Q ... CH7_I CH7_Q");
            draw_channel_legend();
        }

        ImGui::Spacing();
        if (ImGui::BeginTabBar("analysis_tabs")) {
            if (ImGui::BeginTabItem("IQ / Phase")) {
                if (ui.preview_count > 1) {
                    draw_multichannel_plot("I waveform",
                                           ui.preview_i,
                                           ui.preview_count,
                                           -4096.0f,
                                           4096.0f,
                                           190.0f);
                    ImGui::Spacing();
                    draw_multichannel_plot("Q waveform",
                                           ui.preview_q,
                                           ui.preview_count,
                                           -4096.0f,
                                           4096.0f,
                                           190.0f);
                    ImGui::Spacing();
                    draw_phase_plot("Relative phase vs CH0", ui.phase_deg, ui.phase_count);
                } else {
                    ImGui::TextDisabled("Capture IQ to show I/Q waveforms and phase");
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Spectrum")) {
                draw_spectrum_panel(app, ui);
                ImGui::EndTabItem();
            }
#ifdef DFONE_USER_ENABLE_DOA
            if (ImGui::BeginTabItem("IQ / DOA")) {
                draw_doa_panel(app, ui);
                ImGui::EndTabItem();
            }
#endif
            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.055f, 0.065f, 0.070f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    shutdown_app(app);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
