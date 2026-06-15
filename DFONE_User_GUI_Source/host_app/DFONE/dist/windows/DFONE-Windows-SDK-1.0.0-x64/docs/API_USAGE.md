# DFONE Public API 使用说明

本文档面向客户侧应用开发。客户程序只需要使用公开头文件：

```cpp
#include "dfone/session.hpp"
#include "dfone/maintenance.hpp"
```

`dfone/session.hpp` 用于连接设备、配置射频参数、采集 IQ 和录制 IQ。
`dfone/maintenance.hpp` 用于客户可用的板端维护功能，包括 32 hex 板卡唯一标识、
eth0 IP/MAC 配置和 eMMC/QSPI 固件更新。

## 1. 构建和链接

最简单的 CMake 用法是把 DFONE SDK 加入你的工程：

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_dfone_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(/path/to/host_app/DFONE dfone_host_build)

add_executable(my_dfone_app main.cpp)
target_link_libraries(my_dfone_app PRIVATE dfone_host)
```

如果使用交付源码包，可以先构建示例 GUI/CLI：

```bash
cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build -DCMAKE_BUILD_TYPE=Release
cmake --build host_app/DFONE/user_gui/build -j"$(nproc)"
```

默认端口：

| 用途 | 默认值 |
| --- | --- |
| Command TCP | `49208` |
| Data TCP | `49209` |
| Maintenance/Firmware TCP | `49312` |

`DfOneSession` 默认设备 IP 是 `192.168.7.2`。如果客户使用 eth0 静态地址，
请把连接 IP 改成板端实际 eth0 地址，例如 `192.168.1.10`。

## 2. 连接设备并采集 IQ

推荐业务代码使用返回 `bool` 的接口，失败时读取 `last_error()`：

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

便捷别名 `dfone::DfOneDevice` 等同于 `dfone::DfOneSession`。构造函数形式会立即连接，
失败时抛出 `std::runtime_error`，适合快速 demo：

```cpp
dfone::DfOneDevice dev("192.168.7.2");
dev.set_sample_rate_hz(30'720'000);
dev.set_frequency_hz(2'400'000'000ULL);
dev.set_gain_db(30);

dfone::DfOneIqCapture iq = dev.get_iq(65'536);
```

## 3. 配置接口

可以一次性配置：

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

也可以单独设置：

```cpp
dev.set_reference_clock(dfone::DfOneReferenceClock::kExternal);
dev.set_sample_rate_hz(30'720'000);
dev.set_frequency_hz(2'400'000'000ULL);
dev.set_gain_db(30);
```

## 4. IQ 数据格式

`DfOneIqCapture::payload` 是原始 CS16 little-endian 数据，每个 frame 包含 8 通道：

```text
CH0_I CH0_Q CH1_I CH1_Q ... CH7_I CH7_Q
```

每个 I/Q 值是 `int16_t`，每个 frame 是 `8 * 2 * sizeof(int16_t) = 32` 字节。
请求的 frame 数可能被板端限制，实际数量以 `iq.frames` 为准。

解析示例：

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

未补偿 IQ 用于验证或调试：

```cpp
dfone::DfOneIqCapture raw;
if (!dev.capture_uncorrected_iq(65'536, raw)) {
    std::cerr << dev.last_error() << "\n";
}
```

普通业务采集建议优先使用 `capture_iq()`。

## 5. IQ 录制

如果板端支持 record 数据通道，可以使用：

```cpp
dfone::DfOneRecordConfig rec;
rec.channel_mask = 0x1;
rec.length_mb = 64;
rec.output_path = "record.cs16";

if (!dev.record_iq(rec)) {
    std::cerr << dev.last_error() << "\n";
}
```

录制大文件时可以使用进度回调。`fraction` 是 0.0 到 1.0 的比例，
`bytes_written` 和 `total_bytes` 可用于显示已写入容量：

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

可以先查询板端 record 存储状态：

```cpp
bool has_ssd = false;
if (dev.get_record_storage_has_ssd(has_ssd)) {
    std::cout << "SSD present: " << has_ssd << "\n";
}
```

## 6. 读取板卡唯一标识

`get_board_id()` 返回一个 32 个小写 hex 字符组成的稳定板卡标识。该标识由板端
硬件身份材料派生，可用于客户系统中代表单块板卡，但不会暴露原始身份材料。

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

## 7. 读取和设置 eth0 IP/MAC

维护 API 使用独立 maintenance TCP 服务，默认端口 `49312`：

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
    net.mac = "";  // 留空时由板端按默认规则生成或保持当前值
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

DHCP 示例：

```cpp
dfone::DfOneNetworkConfig net;
net.mode = dfone::DfOneNetworkMode::kDhcp;
m.set_network_config(net, output);
```

禁用 eth0 示例：

```cpp
dfone::DfOneNetworkConfig net;
net.mode = dfone::DfOneNetworkMode::kDisabled;
m.set_network_config(net, output);
```

## 8. 固件更新 API

固件更新同样使用 `DfOneMaintenance`，支持 eMMC 和 QSPI `.frm` 包，并通过回调返回进度：

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

更新 QSPI 时只需要修改 target 和文件路径：

```cpp
fw.target = dfone::DfOneFirmwareTarget::kQspi;
fw.package_path = "dfone-qspi.frm";
```

固件更新是阻塞式调用。GUI 程序应把它放到 worker thread 中，并在进度回调里更新界面状态。
如果 `reboot_after=true`，板端更新完成后会重启，TCP 连接断开是正常现象。

## 9. 错误处理和线程建议

- 所有主要 API 返回 `bool`，失败后读取 `last_error()`。
- `get_iq()` / `get_uncorrected_iq()` 是便捷接口；严肃业务建议使用 `capture_iq()` 这类可检查返回值的接口。
- `DfOneSession` 的接口是同步阻塞式，不建议多个线程同时调用同一个实例。
- GUI 程序应把连接、采集、固件更新等耗时操作放到 worker thread。
- 读取板卡唯一标识、固件更新、IP/MAC 设置使用 maintenance 端口；IQ 采集使用 command/data 端口。

## 10. 常见检查项

1. Host 能 ping 通板卡 IP。
2. 板端 command/data 服务正在监听 `49208` 和 `49209`。
3. 板端 maintenance 服务正在监听 `49312`。
4. PC 防火墙没有阻止 TCP 连接。
5. 固件包路径正确，文件不是空文件。
6. 请求 frame 数过大时，以返回的 `iq.frames` 为准。

更多接口细节见 `PUBLIC_API_MANUAL.md`。
