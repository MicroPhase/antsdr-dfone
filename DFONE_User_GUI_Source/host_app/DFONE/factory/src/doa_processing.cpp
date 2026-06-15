#include "dfone/internal/doa_processing.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <limits>
#include <vector>

namespace dfone {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSpeedOfLight = 299792458.0;
constexpr double kPowerFloor = 1.0e-24;
constexpr int kMusicPowerIterations = 100;
constexpr double kVectorNormFloor = 1.0e-18;
constexpr double kCaponDiagonalLoading = 1.0e-6;
constexpr int kDiagnosticsPowerIterations = 80;
constexpr double kRankEigenThreshold = 1.0e-3;

using Complex = std::complex<double>;
using ComplexVector = std::vector<Complex>;
using ComplexMatrix = std::vector<ComplexVector>;

double deg_to_rad(double deg)
{
    return deg * kPi / 180.0;
}

void remove_channel_dc(ComplexVector &samples)
{
    if (samples.empty()) {
        return;
    }

    Complex mean{};
    for (const auto &sample : samples) {
        mean += sample;
    }
    mean /= static_cast<double>(samples.size());
    for (auto &sample : samples) {
        sample -= mean;
    }
}

void normalize_channel_rms(ComplexVector &samples)
{
    if (samples.empty()) {
        return;
    }

    double power = 0.0;
    for (const auto &sample : samples) {
        power += std::norm(sample);
    }
    const double rms = std::sqrt(power / static_cast<double>(samples.size()));
    if (rms <= 1.0e-12) {
        return;
    }
    for (auto &sample : samples) {
        sample /= rms;
    }
}

Complex dot_conj(const ComplexVector &lhs, const ComplexVector &rhs)
{
    Complex value{};
    const std::size_t count = std::min(lhs.size(), rhs.size());
    for (std::size_t i = 0; i < count; ++i) {
        value += std::conj(lhs[i]) * rhs[i];
    }
    return value;
}

double vector_norm(const ComplexVector &v)
{
    double power = 0.0;
    for (const auto &value : v) {
        power += std::norm(value);
    }
    return std::sqrt(power);
}

bool normalize_vector(ComplexVector &v)
{
    const double norm = vector_norm(v);
    if (norm <= kVectorNormFloor) {
        return false;
    }
    for (auto &value : v) {
        value /= norm;
    }
    return true;
}

void orthogonalize_against(ComplexVector &v, const std::vector<ComplexVector> &basis)
{
    for (const auto &u : basis) {
        const Complex projection = dot_conj(u, v);
        for (std::size_t i = 0; i < v.size(); ++i) {
            v[i] -= projection * u[i];
        }
    }
}

ComplexVector mat_vec(const ComplexMatrix &m, const ComplexVector &v)
{
    ComplexVector out(m.size());
    for (std::size_t row = 0; row < m.size(); ++row) {
        Complex sum{};
        for (std::size_t col = 0; col < v.size(); ++col) {
            sum += m[row][col] * v[col];
        }
        out[row] = sum;
    }
    return out;
}

bool solve_linear_system(ComplexMatrix a, ComplexVector b, ComplexVector &x)
{
    const std::size_t n = a.size();
    if (n == 0 || b.size() != n) {
        return false;
    }
    for (const auto &row : a) {
        if (row.size() != n) {
            return false;
        }
    }

    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        double pivot_abs = std::abs(a[col][col]);
        for (std::size_t row = col + 1; row < n; ++row) {
            const double candidate_abs = std::abs(a[row][col]);
            if (candidate_abs > pivot_abs) {
                pivot = row;
                pivot_abs = candidate_abs;
            }
        }
        if (pivot_abs <= kVectorNormFloor) {
            return false;
        }
        if (pivot != col) {
            std::swap(a[pivot], a[col]);
            std::swap(b[pivot], b[col]);
        }

        for (std::size_t row = col + 1; row < n; ++row) {
            const Complex factor = a[row][col] / a[col][col];
            a[row][col] = {};
            for (std::size_t k = col + 1; k < n; ++k) {
                a[row][k] -= factor * a[col][k];
            }
            b[row] -= factor * b[col];
        }
    }

    x.assign(n, {});
    for (std::size_t offset = 0; offset < n; ++offset) {
        const std::size_t row = n - 1 - offset;
        Complex sum = b[row];
        for (std::size_t col = row + 1; col < n; ++col) {
            sum -= a[row][col] * x[col];
        }
        if (std::abs(a[row][row]) <= kVectorNormFloor) {
            return false;
        }
        x[row] = sum / a[row][row];
    }
    return true;
}

ComplexVector make_initial_music_vector(std::size_t size, std::size_t index)
{
    ComplexVector v(size);
    for (std::size_t i = 0; i < size; ++i) {
        const double phase = 0.37 * static_cast<double>((i + 1) * (index + 1));
        v[i] = Complex(std::cos(phase), std::sin(phase));
    }
    return v;
}

std::vector<ComplexVector> estimate_signal_subspace(const ComplexMatrix &covariance,
                                                    std::size_t signal_count)
{
    std::vector<ComplexVector> basis;
    const std::size_t channel_count = covariance.size();
    signal_count = std::min(signal_count, channel_count > 0 ? channel_count - 1 : 0);
    basis.reserve(signal_count);

    for (std::size_t signal = 0; signal < signal_count; ++signal) {
        ComplexVector v = make_initial_music_vector(channel_count, signal);
        orthogonalize_against(v, basis);
        if (!normalize_vector(v)) {
            break;
        }

        for (int iter = 0; iter < kMusicPowerIterations; ++iter) {
            ComplexVector next = mat_vec(covariance, v);
            orthogonalize_against(next, basis);
            if (!normalize_vector(next)) {
                break;
            }
            v = std::move(next);
        }

        orthogonalize_against(v, basis);
        if (!normalize_vector(v)) {
            break;
        }
        basis.push_back(std::move(v));
    }

    return basis;
}

std::vector<double> estimate_dominant_eigenvalues(const ComplexMatrix &covariance)
{
    const std::size_t channel_count = covariance.size();
    std::vector<double> eigenvalues;
    std::vector<ComplexVector> basis;
    eigenvalues.reserve(channel_count);
    basis.reserve(channel_count);

    for (std::size_t idx = 0; idx < channel_count; ++idx) {
        ComplexVector v = make_initial_music_vector(channel_count, idx);
        orthogonalize_against(v, basis);
        if (!normalize_vector(v)) {
            continue;
        }

        for (int iter = 0; iter < kDiagnosticsPowerIterations; ++iter) {
            ComplexVector next = mat_vec(covariance, v);
            orthogonalize_against(next, basis);
            if (!normalize_vector(next)) {
                break;
            }
            v = std::move(next);
        }

        orthogonalize_against(v, basis);
        if (!normalize_vector(v)) {
            continue;
        }

        const ComplexVector cv = mat_vec(covariance, v);
        const double lambda = std::max(0.0, std::real(dot_conj(v, cv)));
        eigenvalues.push_back(lambda);
        basis.push_back(std::move(v));
    }

    std::sort(eigenvalues.begin(), eigenvalues.end(), std::greater<double>());
    return eigenvalues;
}

ComplexMatrix build_covariance_matrix(const ComplexMatrix &samples,
                                      std::size_t sample_count,
                                      double diagonal_loading)
{
    const std::size_t channel_count = samples.size();
    ComplexMatrix covariance(channel_count, ComplexVector(channel_count));
    for (std::size_t row = 0; row < channel_count; ++row) {
        for (std::size_t col = 0; col < channel_count; ++col) {
            Complex sum{};
            for (std::size_t n = 0; n < sample_count; ++n) {
                sum += samples[row][n] * std::conj(samples[col][n]);
            }
            covariance[row][col] = sum / static_cast<double>(sample_count);
        }
    }

    if (diagonal_loading > 0.0 && channel_count > 0) {
        double trace_power = 0.0;
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            trace_power += std::real(covariance[ch][ch]);
        }
        const double loading = std::max(trace_power / static_cast<double>(channel_count),
                                        kPowerFloor) *
                               diagonal_loading;
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            covariance[ch][ch] += loading;
        }
    }

    return covariance;
}

struct DoaPreparedInput {
    std::vector<std::size_t> selected_channels;
    ComplexMatrix samples;
    std::size_t sample_count = 0;
};

bool prepare_doa_input(const IqAnalysis &iq,
                       const DoaConfig &config,
                       DoaPreparedInput &prepared,
                       DoaResult &result)
{
    if (!iq.valid) {
        result.error = "synchronized IQ is not available";
        return false;
    }

    const std::size_t available_channel_count = std::min<std::size_t>(iq.i.size(), iq.q.size());
    prepared.selected_channels.reserve(available_channel_count);
    for (std::size_t ch = 0; ch < available_channel_count; ++ch) {
        const bool enabled =
            ch < config.enabled_channels.size() ? config.enabled_channels[ch] : true;
        if (enabled) {
            prepared.selected_channels.push_back(ch);
        }
    }

    const std::size_t channel_count = prepared.selected_channels.size();
    if (channel_count < 2) {
        result.error = "at least two IQ channels are required";
        return false;
    }

    std::size_t frames = std::numeric_limits<std::size_t>::max();
    for (const std::size_t ch : prepared.selected_channels) {
        frames = std::min({frames, iq.i[ch].size(), iq.q[ch].size()});
    }
    if (frames == std::numeric_limits<std::size_t>::max()) {
        frames = 0;
    }
    if (config.start_sample >= frames) {
        result.error = "DOA start sample is outside the IQ capture";
        return false;
    }

    const std::size_t available = frames - config.start_sample;
    prepared.sample_count = config.sample_count == 0
                                ? available
                                : std::min(config.sample_count, available);
    if (prepared.sample_count < 2) {
        result.error = "not enough IQ samples for DOA";
        return false;
    }

    prepared.samples.assign(channel_count, ComplexVector(prepared.sample_count));
    for (std::size_t out_ch = 0; out_ch < channel_count; ++out_ch) {
        const std::size_t src_ch = prepared.selected_channels[out_ch];
        for (std::size_t n = 0; n < prepared.sample_count; ++n) {
            const std::size_t idx = config.start_sample + n;
            prepared.samples[out_ch][n] = Complex(iq.i[src_ch][idx], iq.q[src_ch][idx]);
        }
        if (config.remove_dc) {
            remove_channel_dc(prepared.samples[out_ch]);
        }
        if (config.normalize_channels) {
            normalize_channel_rms(prepared.samples[out_ch]);
        }
    }

    return true;
}

Complex steering_ula(double spacing_wavelengths, std::size_t channel, double theta)
{
    const double phase = 2.0 * kPi * spacing_wavelengths *
                         std::sin(theta) * static_cast<double>(channel);
    return Complex(std::cos(phase), std::sin(phase));
}

Complex steering_uca(double wavelength,
                     double radius_m,
                     std::size_t src_channel,
                     double theta)
{
    const double element_angle =
        2.0 * kPi * static_cast<double>(src_channel) /
        static_cast<double>(kIqChannelCount);
    const double phase = (2.0 * kPi / wavelength) * radius_m *
                         std::cos(theta - element_angle);
    return Complex(std::cos(phase), std::sin(phase));
}

Complex steering_custom(double wavelength, double x_m, double y_m, double theta)
{
    const double phase = (2.0 * kPi / wavelength) *
                         (x_m * std::cos(theta) + y_m * std::sin(theta));
    return Complex(std::cos(phase), std::sin(phase));
}

bool algorithm_uses_circular_array(DoaAlgorithm algorithm)
{
    return algorithm == DoaAlgorithm::kConventionalCircular ||
           algorithm == DoaAlgorithm::kMusicCircular ||
           algorithm == DoaAlgorithm::kCaponCircular;
}

bool algorithm_uses_custom_array(DoaAlgorithm algorithm)
{
    return algorithm == DoaAlgorithm::kConventionalCustom ||
           algorithm == DoaAlgorithm::kMusicCustom ||
           algorithm == DoaAlgorithm::kCaponCustom;
}

Complex make_array_steering(const DoaConfig &config,
                            const DoaPreparedInput &prepared,
                            double wavelength,
                            double spacing_wavelengths,
                            std::size_t out_ch,
                            double theta)
{
    const std::size_t src_ch = prepared.selected_channels[out_ch];
    if (algorithm_uses_custom_array(config.algorithm)) {
        return steering_custom(wavelength,
                               config.custom_x_m[src_ch],
                               config.custom_y_m[src_ch],
                               theta);
    }
    if (algorithm_uses_circular_array(config.algorithm)) {
        return steering_uca(wavelength, config.array_radius_m, src_ch, theta);
    }
    return steering_ula(spacing_wavelengths, out_ch, theta);
}

double custom_max_spacing_m(const DoaConfig &config, const DoaPreparedInput &prepared)
{
    double max_spacing = 0.0;
    for (std::size_t i = 0; i < prepared.selected_channels.size(); ++i) {
        const std::size_t ch_i = prepared.selected_channels[i];
        for (std::size_t j = i + 1; j < prepared.selected_channels.size(); ++j) {
            const std::size_t ch_j = prepared.selected_channels[j];
            const double dx = config.custom_x_m[ch_i] - config.custom_x_m[ch_j];
            const double dy = config.custom_y_m[ch_i] - config.custom_y_m[ch_j];
            max_spacing = std::max(max_spacing, std::sqrt(dx * dx + dy * dy));
        }
    }
    return max_spacing;
}

void find_peaks(DoaResult &result, std::size_t peak_count)
{
    struct PeakCandidate {
        std::size_t index = 0;
        double value_db = 0.0;
    };

    std::vector<PeakCandidate> candidates;
    const auto &spectrum = result.spectrum_db;
    if (spectrum.empty()) {
        return;
    }

    if (spectrum.size() == 1) {
        result.peaks.push_back({result.angles_deg[0], spectrum[0]});
        return;
    }

    for (std::size_t i = 1; i + 1 < spectrum.size(); ++i) {
        if (spectrum[i] >= spectrum[i - 1] && spectrum[i] >= spectrum[i + 1]) {
            candidates.push_back({i, spectrum[i]});
        }
    }
    if (spectrum.front() >= spectrum[1]) {
        candidates.push_back({0, spectrum.front()});
    }
    if (spectrum.back() >= spectrum[spectrum.size() - 2]) {
        candidates.push_back({spectrum.size() - 1, spectrum.back()});
    }

    std::sort(candidates.begin(), candidates.end(), [](const PeakCandidate &lhs,
                                                       const PeakCandidate &rhs) {
        return lhs.value_db > rhs.value_db;
    });

    result.peaks.clear();
    const double min_separation_deg = std::max(1.0, std::abs(result.angles_deg[1] - result.angles_deg[0]));
    for (const auto &candidate : candidates) {
        const double angle = result.angles_deg[candidate.index];
        bool separated = true;
        for (const auto &peak : result.peaks) {
            if (std::abs(peak.angle_deg - angle) < min_separation_deg) {
                separated = false;
                break;
            }
        }
        if (!separated) {
            continue;
        }
        result.peaks.push_back({angle, candidate.value_db});
        if (result.peaks.size() >= peak_count) {
            break;
        }
    }
}

double estimate_peak_width_deg(const DoaResult &result)
{
    if (result.peaks.empty() || result.angles_deg.size() < 2 ||
        result.spectrum_db.size() != result.angles_deg.size()) {
        return 0.0;
    }

    const double peak_angle = result.peaks.front().angle_deg;
    auto peak_it = std::min_element(result.angles_deg.begin(),
                                    result.angles_deg.end(),
                                    [peak_angle](double lhs, double rhs) {
                                        return std::abs(lhs - peak_angle) <
                                               std::abs(rhs - peak_angle);
                                    });
    const std::size_t peak_index =
        static_cast<std::size_t>(std::distance(result.angles_deg.begin(), peak_it));
    const double threshold_db = result.spectrum_db[peak_index] - 3.0;

    std::size_t left = peak_index;
    while (left > 0 && result.spectrum_db[left - 1] >= threshold_db) {
        --left;
    }
    std::size_t right = peak_index;
    while (right + 1 < result.spectrum_db.size() &&
           result.spectrum_db[right + 1] >= threshold_db) {
        ++right;
    }

    return std::max(0.0, result.angles_deg[right] - result.angles_deg[left]);
}

void update_result_diagnostics(DoaResult &result, const ComplexMatrix &covariance)
{
    const auto eigenvalues = estimate_dominant_eigenvalues(covariance);
    if (!eigenvalues.empty()) {
        const double max_eigen = std::max(eigenvalues.front(), kPowerFloor);
        const double min_eigen = std::max(eigenvalues.back(), kPowerFloor);
        result.estimated_snr_db =
            10.0 * std::log10(std::max((max_eigen - min_eigen) / min_eigen, kPowerFloor));

        for (const double value : eigenvalues) {
            if (value >= max_eigen * kRankEigenThreshold) {
                ++result.covariance_rank;
            }
        }

        if (eigenvalues.size() > 1) {
            double noise_floor = 0.0;
            std::size_t noise_count = 0;
            const std::size_t start = eigenvalues.size() / 2;
            for (std::size_t i = start; i < eigenvalues.size(); ++i) {
                noise_floor += eigenvalues[i];
                ++noise_count;
            }
            noise_floor = noise_count > 0
                              ? std::max(noise_floor / static_cast<double>(noise_count),
                                         kPowerFloor)
                              : min_eigen;
            const double source_threshold = noise_floor * 10.0;
            for (const double value : eigenvalues) {
                if (value > source_threshold) {
                    ++result.estimated_source_count;
                }
            }
            if (result.estimated_source_count == 0 && result.estimated_snr_db > 6.0) {
                result.estimated_source_count = 1;
            }
        }
    }

    if (!result.peaks.empty()) {
        result.peak_margin_db =
            result.peaks.size() > 1
                ? result.peaks[0].value_db - result.peaks[1].value_db
                : std::max(0.0, result.peaks.front().value_db -
                                    *std::min_element(result.spectrum_db.begin(),
                                                      result.spectrum_db.end()));
        result.peak_width_deg = estimate_peak_width_deg(result);
    }

    int score = 0;
    if (result.estimated_snr_db >= 15.0) {
        score += 2;
    } else if (result.estimated_snr_db >= 8.0) {
        score += 1;
    }
    if (result.peak_margin_db >= 8.0) {
        score += 2;
    } else if (result.peak_margin_db >= 3.0) {
        score += 1;
    }
    if (result.peak_width_deg > 0.0 && result.peak_width_deg <= 12.0) {
        score += 1;
    }
    if (result.estimated_source_count == 0 ||
        (result.signal_count > 0 && result.estimated_source_count <= result.signal_count + 1)) {
        score += 1;
    }

    result.confidence = score >= 5 ? "High" : (score >= 3 ? "Medium" : "Low");
}

DoaResult estimate_conventional_doa(const IqAnalysis &iq, const DoaConfig &config)
{
    DoaResult result;
    result.algorithm = config.algorithm;
    result.center_freq_hz = config.center_freq_hz;
    result.element_spacing_m = config.element_spacing_m;
    result.array_radius_m = config.array_radius_m;

    if (config.center_freq_hz <= 0.0) {
        result.error = "center frequency must be positive";
        return result;
    }
    if (config.algorithm == DoaAlgorithm::kConventional && config.element_spacing_m <= 0.0) {
        result.error = "element spacing must be positive";
        return result;
    }
    if (algorithm_uses_circular_array(config.algorithm) && config.array_radius_m <= 0.0) {
        result.error = "array radius must be positive";
        return result;
    }
    if (config.angle_step_deg <= 0.0 || config.angle_max_deg <= config.angle_min_deg) {
        result.error = "invalid angle sweep";
        return result;
    }

    DoaPreparedInput prepared;
    if (!prepare_doa_input(iq, config, prepared, result)) {
        return result;
    }
    const std::size_t channel_count = prepared.selected_channels.size();
    const std::size_t sample_count = prepared.sample_count;

    const double wavelength = kSpeedOfLight / config.center_freq_hz;
    double adjacent_spacing_m = config.element_spacing_m;
    if (algorithm_uses_circular_array(config.algorithm)) {
        adjacent_spacing_m = 2.0 * config.array_radius_m *
                             std::sin(kPi / static_cast<double>(kIqChannelCount));
    } else if (algorithm_uses_custom_array(config.algorithm)) {
        adjacent_spacing_m = custom_max_spacing_m(config, prepared);
        if (adjacent_spacing_m <= 0.0) {
            result.error = "custom array coordinates must contain at least two distinct positions";
            return result;
        }
    }
    const double spacing_wavelengths = adjacent_spacing_m / wavelength;
    result.element_spacing_m = adjacent_spacing_m;
    result.spacing_wavelengths = spacing_wavelengths;
    result.channel_count = channel_count;
    result.sample_count = sample_count;
    if (spacing_wavelengths > 0.5) {
        result.warning = "element spacing is greater than half wavelength; spatial aliasing may occur";
    }

    const ComplexMatrix covariance =
        build_covariance_matrix(prepared.samples, prepared.sample_count, 0.0);

    std::vector<double> power;
    for (double angle = config.angle_min_deg;
         angle <= config.angle_max_deg + config.angle_step_deg * 0.5;
         angle += config.angle_step_deg) {
        const double theta = deg_to_rad(angle);
        double beam_power = 0.0;
        for (std::size_t n = 0; n < sample_count; ++n) {
            Complex y{};
            for (std::size_t out_ch = 0; out_ch < channel_count; ++out_ch) {
                const Complex steering = make_array_steering(config,
                                                             prepared,
                                                             wavelength,
                                                             spacing_wavelengths,
                                                             out_ch,
                                                             theta);
                y += std::conj(steering) * prepared.samples[out_ch][n];
            }
            y /= static_cast<double>(channel_count);
            beam_power += std::norm(y);
        }
        beam_power /= static_cast<double>(sample_count);
        result.angles_deg.push_back(angle);
        power.push_back(std::max(beam_power, kPowerFloor));
    }

    const double max_power = *std::max_element(power.begin(), power.end());
    result.spectrum_db.reserve(power.size());
    for (const double value : power) {
        result.spectrum_db.push_back(10.0 * std::log10(value / max_power));
    }

    find_peaks(result, std::max<std::size_t>(1, config.peak_count));
    update_result_diagnostics(result, covariance);
    result.valid = true;
    return result;
}

DoaResult estimate_subspace_doa(const IqAnalysis &iq, const DoaConfig &config)
{
    DoaResult result;
    result.algorithm = config.algorithm;
    result.center_freq_hz = config.center_freq_hz;
    result.element_spacing_m = config.element_spacing_m;
    result.array_radius_m = config.array_radius_m;

    if (config.center_freq_hz <= 0.0) {
        result.error = "center frequency must be positive";
        return result;
    }
    if (algorithm_uses_circular_array(config.algorithm) && config.array_radius_m <= 0.0) {
        result.error = "array radius must be positive";
        return result;
    }
    if (config.angle_step_deg <= 0.0 || config.angle_max_deg <= config.angle_min_deg) {
        result.error = "invalid angle sweep";
        return result;
    }

    DoaPreparedInput prepared;
    if (!prepare_doa_input(iq, config, prepared, result)) {
        return result;
    }

    const std::size_t channel_count = prepared.selected_channels.size();
    const std::size_t signal_count =
        std::min(std::max<std::size_t>(1, config.signal_count), channel_count - 1);
    result.channel_count = channel_count;
    result.sample_count = prepared.sample_count;
    result.signal_count = signal_count;

    const double wavelength = kSpeedOfLight / config.center_freq_hz;
    double adjacent_spacing_m =
        2.0 * config.array_radius_m * std::sin(kPi / static_cast<double>(kIqChannelCount));
    if (algorithm_uses_custom_array(config.algorithm)) {
        adjacent_spacing_m = custom_max_spacing_m(config, prepared);
        if (adjacent_spacing_m <= 0.0) {
            result.error = "custom array coordinates must contain at least two distinct positions";
            return result;
        }
    }
    const double spacing_wavelengths = adjacent_spacing_m / wavelength;
    result.element_spacing_m = adjacent_spacing_m;
    result.spacing_wavelengths = spacing_wavelengths;
    if (result.spacing_wavelengths > 0.5) {
        result.warning =
            "adjacent element spacing is greater than half wavelength; spatial aliasing may occur";
    }

    const ComplexMatrix covariance =
        build_covariance_matrix(prepared.samples, prepared.sample_count, 0.0);

    const auto signal_subspace = estimate_signal_subspace(covariance, signal_count);
    if (signal_subspace.empty()) {
        result.error = "MUSIC signal subspace estimation failed";
        return result;
    }

    std::vector<double> power;
    for (double angle = config.angle_min_deg;
         angle <= config.angle_max_deg + config.angle_step_deg * 0.5;
         angle += config.angle_step_deg) {
        const double theta = deg_to_rad(angle);
        ComplexVector steering(channel_count);
        for (std::size_t out_ch = 0; out_ch < channel_count; ++out_ch) {
            steering[out_ch] = make_array_steering(config,
                                                   prepared,
                                                   wavelength,
                                                   spacing_wavelengths,
                                                   out_ch,
                                                   theta);
        }

        double denominator = static_cast<double>(channel_count);
        for (const auto &u : signal_subspace) {
            denominator -= std::norm(dot_conj(u, steering));
        }
        denominator = std::max(denominator, kPowerFloor);

        result.angles_deg.push_back(angle);
        power.push_back(1.0 / denominator);
    }

    const double max_power = *std::max_element(power.begin(), power.end());
    result.spectrum_db.reserve(power.size());
    for (const double value : power) {
        result.spectrum_db.push_back(10.0 * std::log10(value / max_power));
    }

    find_peaks(result, std::max<std::size_t>(1, config.peak_count));
    update_result_diagnostics(result, covariance);
    result.valid = true;
    return result;
}

DoaResult estimate_capon_doa(const IqAnalysis &iq, const DoaConfig &config)
{
    DoaResult result;
    result.algorithm = config.algorithm;
    result.center_freq_hz = config.center_freq_hz;
    result.element_spacing_m = config.element_spacing_m;
    result.array_radius_m = config.array_radius_m;

    if (config.center_freq_hz <= 0.0) {
        result.error = "center frequency must be positive";
        return result;
    }
    if (algorithm_uses_circular_array(config.algorithm) && config.array_radius_m <= 0.0) {
        result.error = "array radius must be positive";
        return result;
    }
    if (config.angle_step_deg <= 0.0 || config.angle_max_deg <= config.angle_min_deg) {
        result.error = "invalid angle sweep";
        return result;
    }

    DoaPreparedInput prepared;
    if (!prepare_doa_input(iq, config, prepared, result)) {
        return result;
    }

    const std::size_t channel_count = prepared.selected_channels.size();
    result.channel_count = channel_count;
    result.sample_count = prepared.sample_count;

    const double wavelength = kSpeedOfLight / config.center_freq_hz;
    double adjacent_spacing_m =
        2.0 * config.array_radius_m * std::sin(kPi / static_cast<double>(kIqChannelCount));
    if (algorithm_uses_custom_array(config.algorithm)) {
        adjacent_spacing_m = custom_max_spacing_m(config, prepared);
        if (adjacent_spacing_m <= 0.0) {
            result.error = "custom array coordinates must contain at least two distinct positions";
            return result;
        }
    }
    const double spacing_wavelengths = adjacent_spacing_m / wavelength;
    result.element_spacing_m = adjacent_spacing_m;
    result.spacing_wavelengths = spacing_wavelengths;
    if (result.spacing_wavelengths > 0.5) {
        result.warning =
            "adjacent element spacing is greater than half wavelength; spatial aliasing may occur";
    }

    const ComplexMatrix covariance =
        build_covariance_matrix(prepared.samples, prepared.sample_count, kCaponDiagonalLoading);

    std::vector<double> power;
    for (double angle = config.angle_min_deg;
         angle <= config.angle_max_deg + config.angle_step_deg * 0.5;
         angle += config.angle_step_deg) {
        const double theta = deg_to_rad(angle);
        ComplexVector steering(channel_count);
        for (std::size_t out_ch = 0; out_ch < channel_count; ++out_ch) {
            steering[out_ch] = make_array_steering(config,
                                                   prepared,
                                                   wavelength,
                                                   spacing_wavelengths,
                                                   out_ch,
                                                   theta);
        }

        ComplexVector solved;
        if (!solve_linear_system(covariance, steering, solved)) {
            result.error = "Capon covariance solve failed";
            return result;
        }

        const double denominator = std::max(std::real(dot_conj(steering, solved)), kPowerFloor);
        result.angles_deg.push_back(angle);
        power.push_back(1.0 / denominator);
    }

    const double max_power = *std::max_element(power.begin(), power.end());
    result.spectrum_db.reserve(power.size());
    for (const double value : power) {
        result.spectrum_db.push_back(10.0 * std::log10(value / max_power));
    }

    find_peaks(result, std::max<std::size_t>(1, config.peak_count));
    update_result_diagnostics(result, covariance);
    result.valid = true;
    return result;
}

}  // namespace

DoaResult estimate_doa(const IqAnalysis &iq, const DoaConfig &config)
{
    switch (config.algorithm) {
    case DoaAlgorithm::kConventional:
    case DoaAlgorithm::kConventionalCircular:
    case DoaAlgorithm::kConventionalCustom:
        return estimate_conventional_doa(iq, config);
    case DoaAlgorithm::kMusicCircular:
    case DoaAlgorithm::kMusicCustom:
        return estimate_subspace_doa(iq, config);
    case DoaAlgorithm::kCaponCircular:
    case DoaAlgorithm::kCaponCustom:
        return estimate_capon_doa(iq, config);
    }

    DoaResult result;
    result.error = "unsupported DOA algorithm";
    return result;
}

}  // namespace dfone
