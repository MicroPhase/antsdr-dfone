#include "dfone/session.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void print_usage(const char *program)
{
    std::cout
        << "Usage:\n"
        << "  " << program << " [options]\n\n"
        << "Options:\n"
        << "  --device-ip <ip>       default 192.168.7.2\n"
        << "  --frames <count>       default 65536\n"
        << "  --sample-rate <hz>     default 30720000\n"
        << "  --rx-lo <hz>           default 2400000000\n"
        << "  --rx-gain <db>         default 30\n"
        << "  --uncorrected          capture uncorrected IQ instead of calibrated IQ\n"
        << "  --output <path>        default customer_capture.cs16\n"
        << "  --help                 show this message\n";
}

bool parse_u32(const char *text, std::uint32_t &out)
{
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 0);
    if (!end || *end != '\0' || value > 0xffffffffUL) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool parse_u64(const char *text, std::uint64_t &out)
{
    char *end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 0);
    if (!end || *end != '\0') {
        return false;
    }
    out = static_cast<std::uint64_t>(value);
    return true;
}

bool parse_size(const char *text, std::size_t &out)
{
    std::uint64_t value = 0;
    if (!parse_u64(text, value)) {
        return false;
    }
    out = static_cast<std::size_t>(value);
    return true;
}

bool write_file(const std::string &path, const std::vector<std::uint8_t> &payload)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char *>(payload.data()),
              static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char **argv)
{
    dfone::DfOneEndpoint endpoint;
    std::uint32_t sample_rate_hz = 30'720'000;
    std::uint64_t rx_lo_hz = 2'400'000'000ULL;
    std::uint32_t rx_gain_db = 30;
    std::size_t frames = 65536;
    bool uncorrected = false;
    std::string output_path = "customer_capture.cs16";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next_value = [&](const char *name) -> const char * {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << '\n';
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--device-ip") {
            endpoint.device_ip = next_value("--device-ip");
        } else if (arg == "--frames") {
            if (!parse_size(next_value("--frames"), frames)) {
                std::cerr << "invalid frame count\n";
                return 2;
            }
        } else if (arg == "--sample-rate") {
            if (!parse_u32(next_value("--sample-rate"), sample_rate_hz)) {
                std::cerr << "invalid sample rate\n";
                return 2;
            }
        } else if (arg == "--rx-lo") {
            if (!parse_u64(next_value("--rx-lo"), rx_lo_hz)) {
                std::cerr << "invalid RX LO\n";
                return 2;
            }
        } else if (arg == "--rx-gain") {
            if (!parse_u32(next_value("--rx-gain"), rx_gain_db)) {
                std::cerr << "invalid RX gain\n";
                return 2;
            }
        } else if (arg == "--uncorrected") {
            uncorrected = true;
        } else if (arg == "--output") {
            output_path = next_value("--output");
        } else {
            std::cerr << "unknown option: " << arg << '\n';
            print_usage(argv[0]);
            return 2;
        }
    }

    dfone::DfOneSession device;
    if (!device.connect(endpoint)) {
        std::cerr << "connect failed: " << device.last_error() << '\n';
        return 1;
    }

    if (!device.set_sample_rate_hz(sample_rate_hz)) {
        std::cerr << "set sample rate failed: " << device.last_error() << '\n';
        return 1;
    }

    if (!device.set_frequency_hz(rx_lo_hz)) {
        std::cerr << "set RX LO failed: " << device.last_error() << '\n';
        return 1;
    }

    if (!device.set_gain_db(rx_gain_db)) {
        std::cerr << "set RX gain failed: " << device.last_error() << '\n';
        return 1;
    }

    dfone::DfOneIqCapture capture;
    const bool ok = uncorrected
                        ? device.capture_uncorrected_iq(frames, capture)
                        : device.capture_iq(frames, capture);
    if (!ok) {
        std::cerr << "capture failed: " << device.last_error() << '\n';
        return 1;
    }

    if (!write_file(output_path, capture.payload)) {
        std::cerr << "write failed: " << output_path << '\n';
        return 1;
    }

    std::cout << "captured " << capture.frames << " frames, "
              << capture.payload.size() << " bytes, "
              << capture.channel_count << " channels, "
              << capture.bytes_per_frame << " bytes/frame\n";
    std::cout << "output: " << output_path << '\n';
    return 0;
}
