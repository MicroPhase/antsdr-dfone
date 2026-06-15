#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "dfone/export.hpp"

namespace dfone {

constexpr std::uint16_t kDfOneFirmwareUpdatePort = 49312;

struct DfOneMaintenanceEndpoint {
    std::string device_ip = "192.168.7.2";
    std::uint16_t firmware_update_port = kDfOneFirmwareUpdatePort;
};

enum class DfOneNetworkMode {
    kStatic,
    kDhcp,
    kDisabled,
};

struct DfOneNetworkConfig {
    DfOneNetworkMode mode = DfOneNetworkMode::kStatic;
    std::string mac;
    std::string address = "192.168.1.10";
    std::string netmask = "255.255.255.0";
    std::string gateway;
    std::string dns;
};

enum class DfOneFirmwareTarget {
    kEmmc,
    kQspi,
};

struct DfOneFirmwareUpdateConfig {
    DfOneFirmwareTarget target = DfOneFirmwareTarget::kEmmc;
    std::string package_path;
    bool reboot_after = true;
};

struct DfOneFirmwareProgress {
    double fraction = 0.0;
    int percent = 0;
    std::string stage;
    std::string output;
};

using DfOneFirmwareProgressCallback =
    std::function<void(const DfOneFirmwareProgress &)>;

DFONE_API std::string serialize_network_config(const DfOneNetworkConfig &config);
DFONE_API bool parse_network_config(const std::string &text,
                                    DfOneNetworkConfig &config,
                                    std::string &error);

class DfOneMaintenance {
public:
    DFONE_API DfOneMaintenance();
    DFONE_API explicit DfOneMaintenance(const DfOneMaintenanceEndpoint &endpoint);
    DFONE_API explicit DfOneMaintenance(const std::string &device_ip,
                                        std::uint16_t firmware_update_port =
                                            kDfOneFirmwareUpdatePort);

    DFONE_API void set_endpoint(const DfOneMaintenanceEndpoint &endpoint);
    DFONE_API const DfOneMaintenanceEndpoint &endpoint() const;

    DFONE_API bool get_board_id(std::string &board_id);
    DFONE_API bool read_firmware_status(std::string &output);
    DFONE_API bool get_network_config(DfOneNetworkConfig &config, std::string &raw_config);
    DFONE_API bool set_network_config(const DfOneNetworkConfig &config, std::string &output);
    DFONE_API bool update_firmware(const DfOneFirmwareUpdateConfig &config,
                                   DfOneFirmwareProgressCallback progress_callback,
                                   std::string &output);

    DFONE_API const std::string &last_error() const;

private:
    DfOneMaintenanceEndpoint endpoint_;
    std::string last_error_;
};

}  // namespace dfone
