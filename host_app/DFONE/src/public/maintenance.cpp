#include "dfone/maintenance.hpp"

#include "dfone/internal/socket_compat.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/types.h>
#endif

namespace dfone {
namespace {

namespace fs = std::filesystem;

constexpr int kFirmwareServiceTimeoutMs = 30000;
constexpr std::size_t kMaxFirmwareResponseBytes = 128U * 1024U;
constexpr std::size_t kMaxNetworkConfigBytes = 8U * 1024U;
constexpr std::size_t kBoardIdHexLength = 32U;

struct SocketGuard {
    net::SocketHandle fd = net::kInvalidSocket;

    ~SocketGuard()
    {
        if (net::is_valid(fd)) {
            net::close_socket(fd);
        }
    }

    SocketGuard() = default;
    SocketGuard(const SocketGuard &) = delete;
    SocketGuard &operator=(const SocketGuard &) = delete;
};

std::string trim_copy(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1U);
}

bool parse_key_value_text(const std::string &text,
                          std::unordered_map<std::string, std::string> &values)
{
    values.clear();
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim_copy(line);
        if (line.empty()) {
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            return false;
        }
        std::string key = trim_copy(line.substr(0, eq));
        std::string value = trim_copy(line.substr(eq + 1U));
        if (key.empty()) {
            return false;
        }
        values[key] = value;
    }
    return true;
}

std::string normalize_hex_copy(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        if (std::isxdigit(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return out;
}

bool is_fixed_hex(const std::string &text, std::size_t len)
{
    if (text.size() != len) {
        return false;
    }
    for (unsigned char ch : text) {
        if (!std::isxdigit(ch)) {
            return false;
        }
    }
    return true;
}

bool send_all(net::SocketHandle fd,
              const void *data,
              std::size_t size,
              std::string &error)
{
    const char *ptr = static_cast<const char *>(data);
    std::size_t sent = 0;
    while (sent < size) {
        const int n = net::send_socket(fd, ptr + sent, size - sent);
        if (n < 0) {
            if (net::is_interrupted(net::last_error_code())) {
                continue;
            }
            error = std::string("send failed: ") + net::last_error_message();
            return false;
        }
        if (n == 0) {
            error = "send failed: connection closed";
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool recv_all(net::SocketHandle fd, void *data, std::size_t size, std::string &error)
{
    char *ptr = static_cast<char *>(data);
    std::size_t received = 0;
    while (received < size) {
        const int n = net::recv_socket(fd, ptr + received, size - received);
        if (n < 0) {
            if (net::is_interrupted(net::last_error_code())) {
                continue;
            }
            error = std::string("receive failed: ") + net::last_error_message();
            return false;
        }
        if (n == 0) {
            error = "receive failed: connection closed";
            return false;
        }
        received += static_cast<std::size_t>(n);
    }
    return true;
}

bool recv_line(net::SocketHandle fd,
               std::string &line,
               std::size_t max_len,
               std::string &error)
{
    line.clear();
    while (line.size() < max_len) {
        char ch = '\0';
        const int n = net::recv_socket(fd, &ch, 1);
        if (n < 0) {
            if (net::is_interrupted(net::last_error_code())) {
                continue;
            }
            error = std::string("receive failed: ") + net::last_error_message();
            return false;
        }
        if (n == 0) {
            error = "receive failed: connection closed";
            return false;
        }
        if (ch == '\n') {
            return true;
        }
        if (ch != '\r') {
            line.push_back(ch);
        }
    }
    error = "response line is too long";
    return false;
}

bool connect_service(const DfOneMaintenanceEndpoint &endpoint,
                     SocketGuard &socket,
                     std::string &error)
{
    if (!net::initialize_sockets()) {
        error = "initialize socket runtime failed";
        return false;
    }

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = nullptr;
    const std::string port_text = std::to_string(endpoint.firmware_update_port);
    const int rc = ::getaddrinfo(endpoint.device_ip.c_str(),
                                 port_text.c_str(),
                                 &hints,
                                 &result);
    if (rc != 0) {
        error = std::string("resolve firmware service failed: ") + gai_strerror(rc);
        return false;
    }

    for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        const auto fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (!net::is_valid(fd)) {
            continue;
        }
        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            socket.fd = fd;
            net::set_socket_timeouts(socket.fd, kFirmwareServiceTimeoutMs);
            ::freeaddrinfo(result);
            return true;
        }
        net::close_socket(fd);
    }

    ::freeaddrinfo(result);
    error = std::string("connect firmware service failed: ") + net::last_error_message();
    return false;
}

bool receive_sized_payload(SocketGuard &socket,
                           std::size_t max_size,
                           const char *context,
                           std::string &payload,
                           std::string &error)
{
    std::string response;
    if (!recv_line(socket.fd, response, 1024, error)) {
        return false;
    }
    if (response.rfind("FAIL ", 0) == 0) {
        std::istringstream iss(response.substr(5));
        std::uint64_t size64 = 0;
        iss >> size64;
        if (!iss || size64 > max_size) {
            error = std::string("invalid ") + context + " failure size";
            return false;
        }
        std::vector<char> data(static_cast<std::size_t>(size64));
        if (!data.empty() && !recv_all(socket.fd, data.data(), data.size(), error)) {
            return false;
        }
        error.assign(data.begin(), data.end());
        return false;
    }
    if (response.rfind("OK ", 0) != 0) {
        error = response.rfind("ERR ", 0) == 0 ? response.substr(4) : response;
        return false;
    }

    std::istringstream iss(response.substr(3));
    std::uint64_t size64 = 0;
    iss >> size64;
    if (!iss || size64 > max_size) {
        error = std::string("invalid ") + context + " response size";
        return false;
    }

    std::vector<char> data(static_cast<std::size_t>(size64));
    if (!data.empty() && !recv_all(socket.fd, data.data(), data.size(), error)) {
        return false;
    }
    payload.assign(data.begin(), data.end());
    return true;
}

fs::path resolve_existing_file(const std::string &path_text,
                               const char *what,
                               std::string &error)
{
    if (path_text.empty()) {
        error = std::string(what) + " path is empty";
        return {};
    }

    const fs::path input(path_text);
    std::error_code ec;
    if (input.is_absolute()) {
        if (!fs::is_regular_file(input, ec)) {
            error = std::string(what) + " file not found: " + input.generic_string();
            return {};
        }
        return input;
    }

    const fs::path cwd = fs::current_path(ec);
    if (ec) {
        error = "get current directory failed: " + ec.message();
        return {};
    }

    for (fs::path dir = cwd; !dir.empty(); dir = dir.parent_path()) {
        const fs::path candidate = dir / input;
        if (fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }

    error = std::string(what) + " file not found: " + input.generic_string() +
            " (searched upward from " + cwd.generic_string() + ")";
    return {};
}

void emit_progress(DfOneFirmwareProgressCallback &callback,
                   double fraction,
                   const std::string &stage,
                   const std::string &output)
{
    if (!callback) {
        return;
    }

    DfOneFirmwareProgress progress;
    progress.fraction = std::max(0.0, std::min(1.0, fraction));
    progress.percent = static_cast<int>(progress.fraction * 100.0 + 0.5);
    progress.stage = stage;
    progress.output = output;
    callback(progress);
}

bool send_file_stream(net::SocketHandle fd,
                      const fs::path &path,
                      std::uint64_t size,
                      DfOneFirmwareProgressCallback &callback,
                      double *last_fraction,
                      std::string &error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "open firmware package failed: " + path.generic_string();
        return false;
    }

    std::array<char, 64 * 1024> buffer{};
    std::uint64_t remaining = size;
    std::uint64_t sent = 0;
    while (remaining > 0) {
        const std::size_t chunk =
            static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        in.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (in.gcount() != static_cast<std::streamsize>(chunk)) {
            error = "read firmware package failed";
            return false;
        }
        if (!send_all(fd, buffer.data(), chunk, error)) {
            return false;
        }
        remaining -= chunk;
        sent += chunk;
        const double fraction =
            static_cast<double>(sent) / static_cast<double>(size) * 0.50;
        if (last_fraction) {
            *last_fraction = std::max(*last_fraction, fraction);
        }
        emit_progress(callback,
                      fraction,
                      "Uploading firmware package " + std::to_string(sent) + "/" +
                          std::to_string(size) + " bytes",
                      {});
    }
    return true;
}

const char *firmware_target_token(DfOneFirmwareTarget target)
{
    return target == DfOneFirmwareTarget::kQspi ? "qspi" : "emmc";
}

}  // namespace

std::string serialize_network_config(const DfOneNetworkConfig &config)
{
    std::ostringstream out;
    out << "# DFONE persistent eth0 network configuration\n";
    out << "# Generated by DFONE public maintenance API.\n";
    if (config.mode == DfOneNetworkMode::kStatic) {
        out << "mode=static\n";
    } else if (config.mode == DfOneNetworkMode::kDisabled) {
        out << "mode=none\n";
    } else {
        out << "mode=dhcp\n";
    }
    if (!config.mac.empty()) {
        out << "mac=" << config.mac << '\n';
    }
    if (config.mode == DfOneNetworkMode::kStatic) {
        out << "address=" << config.address << '\n';
        out << "netmask=" << config.netmask << '\n';
        if (!config.gateway.empty()) {
            out << "gateway=" << config.gateway << '\n';
        }
        if (!config.dns.empty()) {
            out << "dns=" << config.dns << '\n';
        }
    }
    return out.str();
}

bool parse_network_config(const std::string &text,
                          DfOneNetworkConfig &config,
                          std::string &error)
{
    std::unordered_map<std::string, std::string> values;
    if (!parse_key_value_text(text, values)) {
        error = "network config has invalid key=value format";
        return false;
    }

    DfOneNetworkConfig parsed;
    const auto mode_it = values.find("mode");
    const std::string mode = mode_it == values.end() ? "static" : mode_it->second;
    if (mode == "static") {
        parsed.mode = DfOneNetworkMode::kStatic;
    } else if (mode == "dhcp") {
        parsed.mode = DfOneNetworkMode::kDhcp;
    } else if (mode == "none" || mode == "manual" || mode == "disabled") {
        parsed.mode = DfOneNetworkMode::kDisabled;
    } else {
        error = "network config has invalid mode: " + mode;
        return false;
    }

    const auto copy_if_present = [&values](const char *key, std::string &target) {
        const auto it = values.find(key);
        if (it != values.end()) {
            target = it->second;
        }
    };
    copy_if_present("mac", parsed.mac);
    copy_if_present("address", parsed.address);
    copy_if_present("ip", parsed.address);
    copy_if_present("netmask", parsed.netmask);
    copy_if_present("gateway", parsed.gateway);
    copy_if_present("dns", parsed.dns);

    if (parsed.mode == DfOneNetworkMode::kStatic &&
        (parsed.address.empty() || parsed.netmask.empty())) {
        error = "static network config requires address and netmask";
        return false;
    }

    config = std::move(parsed);
    error.clear();
    return true;
}

DfOneMaintenance::DfOneMaintenance() = default;

DfOneMaintenance::DfOneMaintenance(const DfOneMaintenanceEndpoint &endpoint)
    : endpoint_(endpoint)
{
}

DfOneMaintenance::DfOneMaintenance(const std::string &device_ip,
                                   std::uint16_t firmware_update_port)
{
    endpoint_.device_ip = device_ip;
    endpoint_.firmware_update_port = firmware_update_port;
}

void DfOneMaintenance::set_endpoint(const DfOneMaintenanceEndpoint &endpoint)
{
    endpoint_ = endpoint;
}

const DfOneMaintenanceEndpoint &DfOneMaintenance::endpoint() const
{
    return endpoint_;
}

bool DfOneMaintenance::get_board_id(std::string &board_id)
{
    SocketGuard socket;
    if (!connect_service(endpoint_, socket, last_error_)) {
        return false;
    }

    const std::string command = "GET_BOARD_ID\n";
    if (!send_all(socket.fd, command.data(), command.size(), last_error_)) {
        return false;
    }

    std::string output;
    if (!receive_sized_payload(socket,
                               kMaxFirmwareResponseBytes,
                               "board ID",
                               output,
                               last_error_)) {
        return false;
    }

    std::unordered_map<std::string, std::string> values;
    if (!parse_key_value_text(output, values)) {
        last_error_ = "board ID response has invalid key=value format";
        return false;
    }
    if (values["format"] != "dfone-board-id-v1") {
        last_error_ = "board ID response has invalid format";
        return false;
    }

    board_id = normalize_hex_copy(values["board_id"]);
    if (!is_fixed_hex(board_id, kBoardIdHexLength)) {
        last_error_ = "board ID has invalid format";
        return false;
    }

    last_error_.clear();
    return true;
}

bool DfOneMaintenance::read_firmware_status(std::string &output)
{
    SocketGuard socket;
    if (!connect_service(endpoint_, socket, last_error_)) {
        return false;
    }

    const std::string command = "STATUS\n";
    if (!send_all(socket.fd, command.data(), command.size(), last_error_)) {
        return false;
    }

    if (!receive_sized_payload(socket,
                               kMaxFirmwareResponseBytes,
                               "firmware status",
                               output,
                               last_error_)) {
        return false;
    }
    last_error_.clear();
    return true;
}

bool DfOneMaintenance::get_network_config(DfOneNetworkConfig &config,
                                          std::string &raw_config)
{
    SocketGuard socket;
    if (!connect_service(endpoint_, socket, last_error_)) {
        return false;
    }

    const std::string command = "GET_NETWORK\n";
    if (!send_all(socket.fd, command.data(), command.size(), last_error_)) {
        return false;
    }

    if (!receive_sized_payload(socket,
                               kMaxNetworkConfigBytes,
                               "network config",
                               raw_config,
                               last_error_)) {
        return false;
    }
    if (raw_config.empty()) {
        config = {};
        last_error_.clear();
        return true;
    }
    if (!parse_network_config(raw_config, config, last_error_)) {
        return false;
    }
    last_error_.clear();
    return true;
}

bool DfOneMaintenance::set_network_config(const DfOneNetworkConfig &config,
                                          std::string &output)
{
    const std::string text = serialize_network_config(config);
    if (text.empty() || text.size() > kMaxNetworkConfigBytes) {
        last_error_ = "network config size is invalid";
        return false;
    }

    SocketGuard socket;
    if (!connect_service(endpoint_, socket, last_error_)) {
        return false;
    }

    const std::string header = "SET_NETWORK " + std::to_string(text.size()) + "\n";
    if (!send_all(socket.fd, header.data(), header.size(), last_error_) ||
        !send_all(socket.fd, text.data(), text.size(), last_error_)) {
        return false;
    }

    if (!receive_sized_payload(socket,
                               kMaxFirmwareResponseBytes,
                               "network config",
                               output,
                               last_error_)) {
        return false;
    }
    last_error_.clear();
    return true;
}

bool DfOneMaintenance::update_firmware(const DfOneFirmwareUpdateConfig &config,
                                       DfOneFirmwareProgressCallback progress_callback,
                                       std::string &output)
{
    const fs::path firmware_path =
        resolve_existing_file(config.package_path, "firmware package", last_error_);
    if (firmware_path.empty()) {
        return false;
    }

    std::error_code ec;
    const auto size = fs::file_size(firmware_path, ec);
    if (ec || size == 0) {
        last_error_ = "read firmware package size failed: " +
                      (ec ? ec.message() : std::string("empty file"));
        return false;
    }

    SocketGuard socket;
    if (!connect_service(endpoint_, socket, last_error_)) {
        return false;
    }

    output.clear();
    double last_fraction = 0.0;
    std::string last_stage = "Starting firmware update";
    const auto report_progress = [&](double fraction,
                                     std::string stage,
                                     const std::string &progress_output) {
        if (fraction >= 0.0) {
            last_fraction = std::max(last_fraction, std::min(1.0, fraction));
        }
        if (!stage.empty()) {
            last_stage = std::move(stage);
        }
        emit_progress(progress_callback, last_fraction, last_stage, progress_output);
    };
    report_progress(0.0, last_stage, output);

    const std::string mode = firmware_target_token(config.target);
    const std::string header = "UPDATE " + mode + " " +
                               (config.reboot_after ? "1" : "0") + " " +
                               std::to_string(size) + "\n";
    if (!send_all(socket.fd, header.data(), header.size(), last_error_) ||
        !send_file_stream(socket.fd,
                          firmware_path,
                          size,
                          progress_callback,
                          &last_fraction,
                          last_error_)) {
        return false;
    }

    bool script_reported_complete = false;
    while (true) {
        std::string response;
        if (!recv_line(socket.fd, response, 1024, last_error_)) {
            if (script_reported_complete) {
                report_progress(1.0,
                                config.reboot_after
                                    ? "Firmware update complete; board rebooting"
                                    : "Firmware update complete",
                                output);
                last_error_.clear();
                return true;
            }
            return false;
        }

        if (response.rfind("PROGRESS ", 0) == 0) {
            std::istringstream iss(response.substr(9));
            int percent = 0;
            iss >> percent;
            std::string stage;
            std::getline(iss, stage);
            stage = trim_copy(stage);
            percent = std::max(0, std::min(100, percent));
            report_progress(static_cast<double>(percent) / 100.0,
                            stage.empty() ? "Firmware update" : stage,
                            output);
            continue;
        }

        if (response.rfind("LOG ", 0) == 0 || response.rfind("DONE ", 0) == 0 ||
            response.rfind("FAIL ", 0) == 0) {
            const bool done = response.rfind("DONE ", 0) == 0;
            const bool fail = response.rfind("FAIL ", 0) == 0;
            const std::size_t prefix_len = done || fail ? 5U : 4U;
            std::istringstream iss(response.substr(prefix_len));
            std::uint64_t size64 = 0;
            iss >> size64;
            if (!iss || size64 > kMaxFirmwareResponseBytes) {
                last_error_ = "invalid firmware update event size";
                return false;
            }
            std::vector<char> data(static_cast<std::size_t>(size64));
            if (!data.empty() &&
                !recv_all(socket.fd, data.data(), data.size(), last_error_)) {
                return false;
            }
            const std::string text(data.begin(), data.end());
            if (!text.empty()) {
                output += text;
                if (text.find("DFONE firmware update complete.") != std::string::npos ||
                    text.find("DFONE firmware update complete") != std::string::npos) {
                    script_reported_complete = true;
                }
                report_progress(done ? 1.0 : -1.0, {}, output);
            }
            if (fail) {
                last_error_ = output.empty() ? std::string("firmware update failed") : output;
                return false;
            }
            if (done) {
                report_progress(1.0,
                                config.reboot_after
                                    ? "Firmware update complete; board rebooting"
                                    : "Firmware update complete",
                                output);
                last_error_.clear();
                return true;
            }
            continue;
        }

        if (response.rfind("ERR ", 0) == 0) {
            last_error_ = response.substr(4);
            return false;
        }

        last_error_ = response.empty() ? std::string("empty firmware update response") : response;
        return false;
    }
}

const std::string &DfOneMaintenance::last_error() const
{
    return last_error_;
}

}  // namespace dfone
