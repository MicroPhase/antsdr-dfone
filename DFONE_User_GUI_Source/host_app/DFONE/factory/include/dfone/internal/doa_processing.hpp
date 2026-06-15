#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "dfone/internal/iq_processing.hpp"
#include "dfone/internal/protocol.hpp"

namespace dfone {

enum class DoaAlgorithm {
    kConventional = 0,
    kConventionalCircular = 1,
    kMusicCircular = 2,
    kCaponCircular = 3,
    kConventionalCustom = 4,
    kMusicCustom = 5,
    kCaponCustom = 6,
};

struct DoaConfig {
    DoaAlgorithm algorithm = DoaAlgorithm::kConventional;
    double center_freq_hz = 2.4e9;
    double element_spacing_m = 0.0625;
    double array_radius_m = 0.065;
    double angle_min_deg = -90.0;
    double angle_max_deg = 90.0;
    double angle_step_deg = 0.5;
    std::size_t start_sample = 0;
    std::size_t sample_count = 4096;
    std::size_t peak_count = 1;
    std::size_t signal_count = 1;
    bool remove_dc = true;
    bool normalize_channels = true;
    std::array<bool, kIqChannelCount> enabled_channels = {true, true, true, true, true, true, true, true};
    std::array<double, kIqChannelCount> custom_x_m = {};
    std::array<double, kIqChannelCount> custom_y_m = {};
};

struct DoaPeak {
    double angle_deg = 0.0;
    double value_db = 0.0;
};

struct DoaResult {
    bool valid = false;
    DoaAlgorithm algorithm = DoaAlgorithm::kConventional;
    double center_freq_hz = 0.0;
    double element_spacing_m = 0.0;
    double array_radius_m = 0.0;
    double spacing_wavelengths = 0.0;
    std::size_t channel_count = 0;
    std::size_t sample_count = 0;
    std::size_t signal_count = 0;
    std::vector<double> angles_deg;
    std::vector<double> spectrum_db;
    std::vector<DoaPeak> peaks;
    double estimated_snr_db = 0.0;
    double peak_margin_db = 0.0;
    double peak_width_deg = 0.0;
    std::size_t covariance_rank = 0;
    std::size_t estimated_source_count = 0;
    std::string confidence;
    std::string warning;
    std::string error;
};

DoaResult estimate_doa(const IqAnalysis &iq, const DoaConfig &config);

}  // namespace dfone
