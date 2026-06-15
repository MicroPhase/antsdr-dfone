#include "dfone/session.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void print_usage(const char *prog)
{
    std::cout
        << "Usage:\n"
        << "  " << prog << " capture [options]\n\n"
        << "Options:\n"
        << "  --device-ip <ip>          default 192.168.7.2\n"
        << "  --cmd-port <port>         default 49208\n"
        << "  --data-port <port>        default 49209\n"
        << "  --sample-rate <hz>        default 30720000\n"
        << "  --rx-lo <hz>              default 2400000000\n"
        << "  --rx-gain <db>            default 30\n"
        << "  --ref-clock <0-3>         default 0\n"
        << "  --frames <count>          default 65536\n"
        << "  --uncorrected             capture uncorrected IQ\n"
        << "  --output <path>           optional app-side .cs16 write\n";
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

bool write_bytes(const std::string &path, const std::vector<std::uint8_t> &payload)
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

class SessionDisconnectGuard {
public:
    explicit SessionDisconnectGuard(dfone::DfOneSession &session)
        : session_(session)
    {
    }

    ~SessionDisconnectGuard()
    {
        session_.disconnect();
    }

    SessionDisconnectGuard(const SessionDisconnectGuard &) = delete;
    SessionDisconnectGuard &operator=(const SessionDisconnectGuard &) = delete;

private:
    dfone::DfOneSession &session_;
};

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 2 || std::string(argv[1]) != "capture") {
        print_usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    dfone::DfOneEndpoint endpoint;
    dfone::DfOneCaptureConfig config;
    std::size_t frames = 65536;
    bool uncorrected = false;
    std::string output_path;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const char *name) -> const char * {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << '\n';
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--device-ip") {
            endpoint.device_ip = need_value("--device-ip");
        } else if (arg == "--cmd-port") {
            std::uint32_t port = 0;
            if (!parse_u32(need_value("--cmd-port"), port) || port > 65535U) {
                std::cerr << "invalid command port\n";
                return 2;
            }
            endpoint.command_port = static_cast<std::uint16_t>(port);
        } else if (arg == "--data-port") {
            std::uint32_t port = 0;
            if (!parse_u32(need_value("--data-port"), port) || port > 65535U) {
                std::cerr << "invalid data port\n";
                return 2;
            }
            endpoint.data_port = static_cast<std::uint16_t>(port);
        } else if (arg == "--sample-rate") {
            if (!parse_u32(need_value("--sample-rate"), config.sample_rate_hz)) {
                std::cerr << "invalid sample rate\n";
                return 2;
            }
        } else if (arg == "--rx-lo") {
            if (!parse_u64(need_value("--rx-lo"), config.rx_lo_hz)) {
                std::cerr << "invalid RX LO\n";
                return 2;
            }
        } else if (arg == "--rx-gain") {
            if (!parse_u32(need_value("--rx-gain"), config.rx_gain_db)) {
                std::cerr << "invalid RX gain\n";
                return 2;
            }
        } else if (arg == "--ref-clock") {
            std::uint32_t source = 0;
            if (!parse_u32(need_value("--ref-clock"), source) || source > 3U) {
                std::cerr << "invalid reference clock\n";
                return 2;
            }
            config.reference_clock = static_cast<dfone::DfOneReferenceClock>(source);
        } else if (arg == "--frames") {
            if (!parse_size(need_value("--frames"), frames)) {
                std::cerr << "invalid frame count\n";
                return 2;
            }
        } else if (arg == "--uncorrected") {
            uncorrected = true;
        } else if (arg == "--output") {
            output_path = need_value("--output");
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown option: " << arg << '\n';
            return 2;
        }
    }

    dfone::DfOneSession session;
    if (!session.connect(endpoint)) {
        std::cerr << "connect failed: " << session.last_error() << '\n';
        return 1;
    }
    SessionDisconnectGuard disconnect_guard(session);

    if (!session.set_reference_clock(config.reference_clock)) {
        std::cerr << "set reference clock failed: " << session.last_error() << '\n';
        return 1;
    }
    if (!session.set_sample_rate_hz(config.sample_rate_hz)) {
        std::cerr << "set sample rate failed: " << session.last_error() << '\n';
        return 1;
    }
    if (!session.set_frequency_hz(config.rx_lo_hz)) {
        std::cerr << "set frequency failed: " << session.last_error() << '\n';
        return 1;
    }
    if (!session.set_gain_db(config.rx_gain_db)) {
        std::cerr << "set gain failed: " << session.last_error() << '\n';
        return 1;
    }

    dfone::DfOneIqCapture capture;
    const bool ok = uncorrected
                        ? session.capture_uncorrected_iq(frames, capture)
                        : session.capture_iq(frames, capture);
    if (!ok) {
        std::cerr << "capture failed: " << session.last_error() << '\n';
        return 1;
    }

    std::cout << "capture ok\n"
              << "  kind=" << (capture.kind == dfone::DfOneIqKind::kCalibrated
                                   ? "calibrated"
                                   : "uncorrected")
              << '\n'
              << "  sample_rate_hz=" << capture.sample_rate_hz << '\n'
              << "  rx_lo_hz=" << capture.rx_lo_hz << '\n'
              << "  frames=" << capture.frames << '\n'
              << "  channel_count=" << capture.channel_count << '\n'
              << "  bytes=" << capture.payload.size() << '\n';

    if (!output_path.empty()) {
        if (!write_bytes(output_path, capture.payload)) {
            std::cerr << "write output failed: " << output_path << '\n';
            return 1;
        }
        std::cout << "  output=" << output_path << '\n';
    }

    return 0;
}
