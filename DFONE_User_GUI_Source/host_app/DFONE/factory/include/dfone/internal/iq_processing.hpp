#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "dfone/internal/protocol.hpp"

namespace dfone {

struct IqAnalysis {
    bool valid = false;
    std::size_t frames = 0;
    std::size_t plot_samples = 0;
    std::size_t segments = 0;
    std::size_t segment_len = 0;
    std::vector<std::vector<float>> i;
    std::vector<std::vector<float>> q;
    std::vector<std::vector<float>> phase_deg;
    std::array<std::int16_t, kPhaseCompCount> phase_counts{};
    std::array<double, kPhaseCompCount> phase_degrees{};
    std::string error;
};

bool write_binary_file(const std::string &path, const std::vector<std::uint8_t> &data);

bool compute_relative_phase_counts(const std::vector<std::uint8_t> &iq_payload,
                                   std::array<std::int16_t, kPhaseCompCount> &out_phase);

bool compute_relative_phase(const std::vector<std::uint8_t> &iq_payload,
                            std::array<std::int16_t, kPhaseCompCount> &out_counts,
                            std::array<double, kPhaseCompCount> &out_degrees);

std::array<std::int16_t, kPhaseCompCount> subtract_phase_counts(
    const std::array<std::int16_t, kPhaseCompCount> &lhs,
    const std::array<std::int16_t, kPhaseCompCount> &rhs);

std::array<double, kPhaseCompCount> subtract_phase_degrees(
    const std::array<double, kPhaseCompCount> &lhs,
    const std::array<double, kPhaseCompCount> &rhs);

std::vector<float> extract_iq_plot_series(const std::vector<std::uint8_t> &iq_payload,
                                          std::size_t channel,
                                          bool imag,
                                          std::size_t max_frames);

IqAnalysis build_iq_analysis(const std::vector<std::uint8_t> &iq_payload,
                             std::size_t channel_count,
                             std::size_t segment_len,
                             std::size_t plot_samples);

}  // namespace dfone
