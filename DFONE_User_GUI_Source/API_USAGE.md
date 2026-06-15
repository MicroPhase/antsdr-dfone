# DFONE Public API Usage

This document is a quick-start guide for customer applications. Include only
the public headers:

```cpp
#include "dfone/session.hpp"
#include "dfone/maintenance.hpp"
```

`dfone/session.hpp` provides device connection, RF configuration, IQ capture,
and IQ record APIs. `dfone/maintenance.hpp` provides customer-available board
maintenance APIs: 32-hex board ID, eth0 IP/MAC configuration, and eMMC/QSPI
firmware update.

## 1. Build And Link

Minimal CMake integration:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_dfone_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(/path/to/host_app/DFONE dfone_host_build)

add_executable(my_dfone_app main.cpp)
target_link_libraries(my_dfone_app PRIVATE dfone_host)
```

Build the bundled examples:

```bash
cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build -DCMAKE_BUILD_TYPE=Release
cmake --build host_app/DFONE/user_gui/build -j"$(nproc)"
```

Default ports:

| Purpose | Default |
| --- | --- |
| Command TCP | `49208` |
| Data TCP | `49209` |
| Maintenance/Firmware TCP | `49312` |

The default `DfOneSession` device IP is `192.168.7.2`. If eth0 static IP is
used, connect to the configured eth0 address, for example `192.168.1.10`.

## 2. Connect And Capture IQ

For production code, prefer the `bool` returning APIs and read `last_error()`
on failure:

```cpp
#include "dfone/session.hpp"

#include <fstream>
#include <iostream>

int main()
{
    dfone::DfOneSession dev;
    if (!dev.open("192.168.7.2")) {
        std::cerr << "open failed: " << dev.last_error() << "\n";
        return 1;
    }

    if (!dev.set_sample_rate_hz(30'720'000) ||
        !dev.set_frequency_hz(2'400'000'000ULL) ||
        !dev.set_gain_db(30)) {
        std::cerr << "configure failed: " << dev.last_error() << "\n";
        return 1;
    }

    dfone::DfOneIqCapture iq;
    if (!dev.capture_iq(65'536, iq)) {
        std::cerr << "capture failed: " << dev.last_error() << "\n";
        return 1;
    }

    std::ofstream out("iq.cs16", std::ios::binary);
    out.write(reinterpret_cast<const char *>(iq.payload.data()),
              static_cast<std::streamsize>(iq.payload.size()));

    dev.close();
    return 0;
}
```

`dfone::DfOneDevice` is an alias of `dfone::DfOneSession`. The constructor form
connects immediately and throws `std::runtime_error` on failure:

```cpp
dfone::DfOneDevice dev("192.168.7.2");
dev.set_sample_rate_hz(30'720'000);
dev.set_frequency_hz(2'400'000'000ULL);
dev.set_gain_db(30);

dfone::DfOneIqCapture iq = dev.get_iq(65'536);
```

## 3. Configure RF Parameters

Configure all capture parameters at once:

```cpp
dfone::DfOneCaptureConfig cfg;
cfg.reference_clock = dfone::DfOneReferenceClock::kDefault;
cfg.sample_rate_hz = 30'720'000;
cfg.rx_lo_hz = 2'400'000'000ULL;
cfg.rx_gain_db = 30;

if (!dev.configure(cfg)) {
    std::cerr << dev.last_error() << "\n";
}
```

Or set individual parameters:

```cpp
dev.set_reference_clock(dfone::DfOneReferenceClock::kExternal);
dev.set_sample_rate_hz(30'720'000);
dev.set_frequency_hz(2'400'000'000ULL);
dev.set_gain_db(30);
```

## 4. IQ Payload Format

`DfOneIqCapture::payload` is raw CS16 little-endian data. Each frame contains 8
channels:

```text
CH0_I CH0_Q CH1_I CH1_Q ... CH7_I CH7_Q
```

Each I/Q value is `int16_t`, and each frame is `8 * 2 * sizeof(int16_t) = 32`
bytes. The requested frame count may be limited by the board; use
`iq.frames` as the actual returned frame count.

Parsing example:

```cpp
static std::int16_t read_i16_le(const std::uint8_t *p)
{
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8U));
}

for (std::size_t frame = 0; frame < iq.frames; ++frame) {
    const std::uint8_t *base = iq.payload.data() + frame * iq.bytes_per_frame;
    for (std::size_t ch = 0; ch < iq.channel_count; ++ch) {
        const std::int16_t i = read_i16_le(base + ch * 4);
        const std::int16_t q = read_i16_le(base + ch * 4 + 2);
        (void)i;
        (void)q;
    }
}
```

Uncorrected IQ is available for verification/debug:

```cpp
dfone::DfOneIqCapture raw;
if (!dev.capture_uncorrected_iq(65'536, raw)) {
    std::cerr << dev.last_error() << "\n";
}
```

For normal applications, prefer `capture_iq()`.

## 5. IQ Record

If the board supports record streaming:

```cpp
dfone::DfOneRecordConfig rec;
rec.channel_mask = 0x1;
rec.length_mb = 64;
rec.output_path = "record.cs16";

if (!dev.record_iq(rec)) {
    std::cerr << dev.last_error() << "\n";
}
```

Use the progress overload for large records:

```cpp
if (!dev.record_iq(
        rec,
        [](const dfone::DfOneRecordProgress &progress) {
            std::cout << "\rrecord "
                      << progress.fraction * 100.0 << "% "
                      << progress.bytes_written << "/"
                      << progress.total_bytes << " bytes" << std::flush;
        })) {
    std::cerr << "\n" << dev.last_error() << "\n";
}
```

Query record storage status:

```cpp
bool has_ssd = false;
if (dev.get_record_storage_has_ssd(has_ssd)) {
    std::cout << "SSD present: " << has_ssd << "\n";
}
```

## 6. Read Board ID

`get_board_id()` returns a stable 32-character lowercase hex board identifier.
It is derived on the board from hardware identity material, so customer
software can identify a board without receiving the raw identity material.

```cpp
#include "dfone/maintenance.hpp"

#include <iostream>

int main()
{
    dfone::DfOneMaintenance m("192.168.7.2");

    std::string board_id;
    if (!m.get_board_id(board_id)) {
        std::cerr << "read board ID failed: " << m.last_error() << "\n";
        return 1;
    }

    std::cout << "board_id=" << board_id << "\n";
    return 0;
}
```

## 7. Read And Set eth0 IP/MAC

Maintenance APIs use the independent maintenance TCP service, default port
`49312`:

```cpp
#include "dfone/maintenance.hpp"

#include <iostream>

int main()
{
    dfone::DfOneMaintenance m("192.168.7.2");

    dfone::DfOneNetworkConfig net;
    std::string raw;
    if (m.get_network_config(net, raw)) {
        std::cout << "current config:\n" << raw << "\n";
    } else {
        std::cerr << m.last_error() << "\n";
    }

    net.mode = dfone::DfOneNetworkMode::kStatic;
    net.mac = "";
    net.address = "192.168.1.10";
    net.netmask = "255.255.255.0";
    net.gateway = "";
    net.dns = "";

    std::string output;
    if (!m.set_network_config(net, output)) {
        std::cerr << "set network failed: " << m.last_error() << "\n";
        return 1;
    }

    std::cout << output << "\n";
    std::cout << "reboot board to apply the new eth0 config\n";
    return 0;
}
```

DHCP:

```cpp
dfone::DfOneNetworkConfig net;
net.mode = dfone::DfOneNetworkMode::kDhcp;
m.set_network_config(net, output);
```

Disable eth0:

```cpp
dfone::DfOneNetworkConfig net;
net.mode = dfone::DfOneNetworkMode::kDisabled;
m.set_network_config(net, output);
```

## 8. Firmware Update

Firmware update supports eMMC and QSPI `.frm` packages and reports progress by
callback:

```cpp
#include "dfone/maintenance.hpp"

#include <iostream>

int main()
{
    dfone::DfOneMaintenance m("192.168.7.2");

    std::string status;
    if (!m.read_firmware_status(status)) {
        std::cerr << "service check failed: " << m.last_error() << "\n";
        return 1;
    }
    std::cout << status << "\n";

    dfone::DfOneFirmwareUpdateConfig fw;
    fw.target = dfone::DfOneFirmwareTarget::kEmmc;
    fw.package_path = "dfone-emmc.frm";
    fw.reboot_after = true;

    std::string output;
    const bool ok = m.update_firmware(
        fw,
        [](const dfone::DfOneFirmwareProgress &p) {
            std::cout << p.percent << "% " << p.stage << "\n";
        },
        output);

    if (!ok) {
        std::cerr << "update failed: " << m.last_error() << "\n";
        return 1;
    }

    std::cout << output << "\n";
    return 0;
}
```

QSPI update:

```cpp
fw.target = dfone::DfOneFirmwareTarget::kQspi;
fw.package_path = "dfone-qspi.frm";
```

`update_firmware()` is blocking. GUI applications should run it in a worker
thread and update UI state from the progress callback. If `reboot_after=true`,
the board reboots after completion; TCP disconnection is expected.

## 9. Error Handling And Threading

- Most APIs return `bool`; read `last_error()` after failure.
- `get_iq()` / `get_uncorrected_iq()` are convenience helpers. Production code
  should prefer `capture_iq()` and check the return value.
- `DfOneSession` methods are synchronous and should not be called concurrently
  from multiple threads on the same instance.
- GUI applications should run connect, capture, and firmware update operations
  in a worker thread.
- Board ID, firmware update and IP/MAC configuration use the maintenance port;
  IQ capture uses the command/data ports.

## 10. Checklist

1. Host can ping the board IP.
2. Board command/data services listen on `49208` and `49209`.
3. Board maintenance service listens on `49312`.
4. PC firewall allows TCP connections.
5. Firmware package path is correct and the file is not empty.
6. If a large IQ request is truncated, use the returned `iq.frames`.

For complete API details, see `host_app/DFONE/PUBLIC_API_MANUAL.md`.
