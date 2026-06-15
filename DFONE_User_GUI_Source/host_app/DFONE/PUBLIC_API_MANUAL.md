# DFONE Public API 用户手册

本文档面向使用 DFONE Host SDK 的用户程序开发者。用户程序只需要包含：

```cpp
#include "dfone/session.hpp"
#include "dfone/maintenance.hpp"
```

不要包含：

```cpp
#include "dfone/internal/..."
```

Public API 的目标是提供稳定的“连接设备、配置采集参数、获取 IQ 数据”和“客户可用板端维护”接口。生产、调试和内部维护接口不属于本手册范围。

推荐用户优先使用直观对象式接口：

```cpp
dfone::DfOneDevice dev("192.168.7.2");
dev.set_sample_rate_hz(30'720'000);
dev.set_frequency_hz(2'400'000'000ULL);
dev.set_gain_db(30);

auto iq = dev.get_iq(65536);
```

构造函数会立即连接设备；如果连接失败，会抛出 `std::runtime_error`。如果不希望使用异常，也可以使用默认构造 + `open()`，通过返回值判断连接是否成功。

## 1. 能力概览

当前 Public API 支持：

- 连接/断开 DFONE 设备。
- 配置同步采集模式。
- 设置参考时钟、采样率、RX LO、RX gain。
- 获取板端已校准/已补偿的 8 通道 IQ。
- 获取未补偿的 8 通道 IQ。
- 返回采集元数据和原始 CS16 payload。
- 读取板卡 32 个 hex 字符唯一标识 `board_id`。
- 读写板端 eth0 持久化 IP/MAC 配置。
- eMMC/QSPI 固件 `.frm` 更新，并通过回调返回上传和刷写进度。
- 通过 `last_error()` 获取最近错误说明。

当前 Public API 保留但未实现：

- `DfOneWorkMode::kIndependentAd9361`

如果设置独立 AD9361 工作模式，API 会返回失败，并给出错误字符串。

## 2. 构建 SDK

在 Host 工程目录构建：

```bash
cd host_app/DFONE
cmake -S . -B build
cmake --build build -j
```

生成：

```text
build/libdfone_host.a
```

其中客户程序只需要链接：

```text
libdfone_host.a
```

并包含：

```text
public/include
```

## 3. 构建 Public API 示例

客户示例位于：

```text
host_app/DFONE/user_gui/
```

构建命令：

```bash
cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build
cmake --build host_app/DFONE/user_gui/build -j
```

如果只需要命令行示例：

```bash
cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build -DDFONE_USER_BUILD_GUI=OFF
cmake --build host_app/DFONE/user_gui/build --target dfone_user_cli -j
```

运行 CLI 示例：

```bash
./host_app/DFONE/user_gui/build/dfone_user_cli capture \
  --device-ip 192.168.7.2 \
  --sample-rate 30720000 \
  --rx-lo 2400000000 \
  --rx-gain 30 \
  --frames 65536 \
  --output iq.cs16
```

采集未补偿 IQ：

```bash
./host_app/DFONE/user_gui/build/dfone_user_cli capture --uncorrected
```

## 4. 链接到自己的程序

最小 CMake 示例：

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_dfone_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(/path/to/host_app/DFONE dfone_host_build)

add_executable(my_dfone_app main.cpp)
target_link_libraries(my_dfone_app PRIVATE dfone_host)
```

如果使用已安装的库，可以按你的安装路径添加 include 和 library。

## 5. 快速开始

```cpp
#include "dfone/session.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>

int main()
{
    dfone::DfOneDevice dev("192.168.7.2");

    if (!dev.set_sample_rate_hz(30'720'000) ||
        !dev.set_frequency_hz(2'400'000'000ULL) ||
        !dev.set_gain_db(30)) {
        std::cerr << "configure failed: " << dev.last_error() << "\n";
        return 1;
    }

    dfone::DfOneIqCapture capture = dev.get_iq(65536);
    if (capture.payload.empty()) {
        std::cerr << "capture failed: " << dev.last_error() << "\n";
        return 1;
    }

    std::ofstream out("iq.cs16", std::ios::binary);
    out.write(reinterpret_cast<const char *>(capture.payload.data()),
              static_cast<std::streamsize>(capture.payload.size()));

    std::cout << "frames=" << capture.frames
              << " bytes=" << capture.payload.size() << "\n";
    return 0;
}
```

## 6. API 头文件

公开头文件：

```cpp
#include "dfone/session.hpp"
```

命名空间：

```cpp
namespace dfone
```

Public API 不依赖 ImGui，不暴露内部协议头文件。

## 7. 常量

```cpp
constexpr std::size_t kDfOneApiChannelCount = 8;
constexpr std::size_t kDfOneApiBytesPerFrame = 32;
```

含义：

| 常量 | 说明 |
| --- | --- |
| `kDfOneApiChannelCount` | 固定 8 通道 |
| `kDfOneApiBytesPerFrame` | 每个 IQ frame 32 字节 |

## 8. 枚举

### 8.1 `DfOneWorkMode`

```cpp
enum class DfOneWorkMode {
    kSynchronizedCapture,
    kIndependentAd9361,
};
```

| 值 | 说明 |
| --- | --- |
| `kSynchronizedCapture` | 4 片 AD9361 同步工作，输出 8 通道同步 IQ |
| `kIndependentAd9361` | 预留，当前未实现 |

### 8.2 `DfOneReferenceClock`

```cpp
enum class DfOneReferenceClock : std::uint32_t {
    kDefault = 0,
    kExternal = 1,
    kSource2 = 2,
    kSource3 = 3,
};
```

对应板端 `ref_clk_sel[1:0]`。

### 8.3 `DfOneIqKind`

```cpp
enum class DfOneIqKind {
    kCalibrated,
    kUncorrected,
};
```

| 值 | 说明 |
| --- | --- |
| `kCalibrated` | 板端相位补偿后的 IQ |
| `kUncorrected` | 未补偿 raw IQ |

### 8.4 `DfOneIqSampleFormat`

```cpp
enum class DfOneIqSampleFormat {
    kCs16LeInterleavedChannels,
};
```

当前只支持一种格式：8 通道交织 CS16 little-endian。

## 9. 数据结构

### 9.1 `DfOneEndpoint`

```cpp
struct DfOneEndpoint {
    std::string device_ip = "192.168.7.2";
    std::uint16_t command_port = 49208;
    std::uint16_t data_port = 49209;
};
```

| 字段 | 说明 |
| --- | --- |
| `device_ip` | 板卡 IPv4 地址 |
| `command_port` | 控制 TCP 端口，默认 49208 |
| `data_port` | IQ 数据 TCP 端口，默认 49209 |

### 9.2 `DfOneCaptureConfig`

```cpp
struct DfOneCaptureConfig {
    DfOneWorkMode work_mode = DfOneWorkMode::kSynchronizedCapture;
    DfOneReferenceClock reference_clock = DfOneReferenceClock::kDefault;
    std::uint32_t sample_rate_hz = 30'720'000;
    std::uint64_t rx_lo_hz = 2'400'000'000ULL;
    std::uint32_t rx_gain_db = 30;
};
```

| 字段 | 说明 |
| --- | --- |
| `work_mode` | 当前应使用 `kSynchronizedCapture` |
| `reference_clock` | 参考时钟选择 |
| `sample_rate_hz` | 采样率，单位 Hz |
| `rx_lo_hz` | RX LO，单位 Hz |
| `rx_gain_db` | RX 增益，单位 dB |

`configure()` 会把这些配置下发到板端。板端在设置 sample rate 或 RX LO 时会自动执行相关同步/内部校准流程，因此这两个操作可能比普通命令耗时更长。

### 9.3 `DfOneIqCapture`

```cpp
struct DfOneIqCapture {
    DfOneIqKind kind;
    DfOneIqSampleFormat sample_format;
    std::uint32_t sample_rate_hz;
    std::uint64_t rx_lo_hz;
    std::size_t frames;
    std::size_t channel_count;
    std::size_t bytes_per_frame;
    std::vector<std::uint8_t> payload;
};
```

| 字段 | 说明 |
| --- | --- |
| `kind` | 校准后 IQ 或未补偿 IQ |
| `sample_format` | payload 格式 |
| `sample_rate_hz` | 采集对应采样率 |
| `rx_lo_hz` | 采集对应 RX LO |
| `frames` | 实际返回 frame 数 |
| `channel_count` | 当前为 8 |
| `bytes_per_frame` | 当前为 32 |
| `payload` | 原始 CS16 IQ 字节 |

`frames` 根据实际收到的 payload 长度计算：

```text
frames = payload.size() / bytes_per_frame
```

## 10. `DfOneSession`

`DfOneSession` 也可以通过别名 `DfOneDevice` 使用：

```cpp
using DfOneDevice = DfOneSession;
```

建议普通用户代码使用 `DfOneDevice` 这个名字，因为它更接近“创建一个设备对象，然后调用方法”的直觉。

### 10.1 生命周期

推荐写法：

```cpp
dfone::DfOneDevice dev("192.168.7.2");
dev.close();
```

如果构造时设备不存在、IP 错误或端口无法连接，构造函数会抛出异常：

```cpp
try {
    dfone::DfOneDevice dev("192.168.7.2");
} catch (const std::runtime_error &e) {
    std::cerr << "open DFONE failed: " << e.what() << "\n";
}
```

不使用异常的写法：

```cpp
dfone::DfOneDevice dev;
if (!dev.open("192.168.7.2")) {
    std::cerr << dev.last_error() << "\n";
}
```

显式写法：

```cpp
dfone::DfOneSession session;
session.connect(endpoint);
session.configure(config);
session.capture_iq(frames, capture);
session.disconnect();
```

析构时会自动释放内部资源，但建议用户在结束使用时显式调用 `disconnect()`。

`DfOneSession` 不可拷贝，可以移动。

### 10.2 构造函数

```cpp
DfOneSession();
explicit DfOneSession(const DfOneEndpoint &endpoint);
explicit DfOneSession(const std::string &device_ip,
                      std::uint16_t command_port = 49208,
                      std::uint16_t data_port = 49209);
```

| 构造函数 | 行为 |
| --- | --- |
| `DfOneSession()` | 只创建对象，不连接设备 |
| `DfOneSession(endpoint)` | 创建对象并立即连接 endpoint |
| `DfOneSession(device_ip, command_port, data_port)` | 创建对象并立即连接指定 IP/端口 |

带 endpoint/IP 的构造函数连接失败时会抛出 `std::runtime_error`。

### 10.3 便捷对象接口

这些接口适合绝大多数用户程序：

```cpp
bool open(const std::string &device_ip = "192.168.7.2",
          std::uint16_t command_port = 49208,
          std::uint16_t data_port = 49209);
void close();
bool is_open() const;

bool set_sample_rate_hz(std::uint32_t sample_rate_hz);
bool set_frequency_hz(std::uint64_t rx_lo_hz);
bool set_gain_db(std::uint32_t gain_db);

DfOneIqCapture get_iq(std::size_t frames);
DfOneIqCapture get_uncorrected_iq(std::size_t frames);
std::vector<std::uint8_t> get_iq_payload(std::size_t frames);
std::vector<std::uint8_t> get_uncorrected_iq_payload(std::size_t frames);
```

示例：

```cpp
dfone::DfOneDevice dev("192.168.7.2");

dev.set_sample_rate_hz(30'720'000);
dev.set_frequency_hz(2'400'000'000ULL);
dev.set_gain_db(30);

auto iq = dev.get_iq(65536);
if (iq.payload.empty()) {
    std::cerr << dev.last_error() << "\n";
}
```

说明：

- `open()` 是 `connect(DfOneEndpoint)` 的便捷封装。
- `close()` 是 `disconnect()` 的别名。
- `is_open()` 是 `connected()` 的别名。
- `set_frequency_hz()` 当前设置的是同步模式下共同 RX LO；板端 active TX LO 也会跟随设置。
- `get_iq()` 内部调用 `capture_iq()` 并直接返回 `DfOneIqCapture`。
- `get_iq_payload()` 只返回原始 payload，适合用户已经知道采样率和数据格式的简单场景。
- 如果 `get_iq()` 失败，会返回空 payload 的 capture；错误原因通过 `last_error()` 获取。

### 10.4 显式接口

下面这些接口保留给需要明确 endpoint/config/out 参数的程序。

#### 10.4.1 `connect`

```cpp
bool connect(const DfOneEndpoint &endpoint);
```

作用：

- 连接 command TCP 端口。
- 连接 data TCP 端口。
- 初始化内部 Host 状态。

返回：

- `true`：连接成功。
- `false`：连接失败，可读取 `last_error()`。

连接失败常见原因：

- IP 地址错误。
- 板端程序未运行。
- 端口被防火墙阻断。
- command/data 端口不匹配。

#### 10.4.2 `disconnect`

```cpp
void disconnect();
```

关闭 command/data socket，并清理内部状态。重复调用是安全的。

#### 10.4.3 `connected`

```cpp
bool connected() const;
```

返回当前 session 是否持有有效 Host 对象。

#### 10.4.4 `configure`

```cpp
bool configure(const DfOneCaptureConfig &config);
```

作用：

- 设置参考时钟。
- 设置采样率和 RF 带宽。
- 设置 RX gain。
- 设置 RX LO。
- 更新 session 内部配置缓存。

注意：

- 必须先 `connect()`。
- `work_mode` 必须是 `kSynchronizedCapture`。
- 如果配置项与上次配置相同，内部实现会避免重复下发部分命令。

#### 10.4.5 单独设置接口

```cpp
bool set_work_mode(DfOneWorkMode mode);
bool set_reference_clock(DfOneReferenceClock source);
bool set_rx_lo(std::uint64_t rx_lo_hz);
bool set_rx_gain(std::uint32_t gain_db);
bool set_sample_rate(std::uint32_t sample_rate_hz);
```

这些接口适合 GUI 或交互式程序在已连接状态下修改单个参数。便捷接口
`set_frequency_hz()`、`set_gain_db()`、`set_sample_rate_hz()` 分别对应这里的
`set_rx_lo()`、`set_rx_gain()`、`set_sample_rate()`。

注意：

- `set_rx_lo()` 会使板端设置所有 RX LO，同时 active TX LO 也会跟随设置。
- `set_rx_lo()` 在板端会触发 AD9361 MCS、FPGA baseband sync 和必要的板端同步流程。
- `set_sample_rate()` 在板端会更新所有 AD9361 的 RX/TX sampling frequency 和 RF bandwidth，并触发内部校准。

#### 10.4.6 `capture_iq`

```cpp
bool capture_iq(std::size_t frames, DfOneIqCapture &out);
```

采集板端当前补偿状态下的同步 IQ。

参数：

| 参数 | 说明 |
| --- | --- |
| `frames` | 请求采集的 IQ frame 数 |
| `out` | 输出采集结果 |

返回：

- `true`：采集成功。
- `false`：采集失败，读取 `last_error()`。

#### 10.4.7 `capture_uncorrected_iq`

```cpp
bool capture_uncorrected_iq(std::size_t frames, DfOneIqCapture &out);
```

采集未补偿 raw IQ。该接口通常用于调试或验证，不是普通业务采集首选。

#### 10.4.8 `config`

#### 10.4.8 `record_iq`

```cpp
bool record_iq(const DfOneRecordConfig &record);
bool record_iq(const DfOneRecordConfig &record,
               DfOneRecordProgressCallback progress_callback);
```

按 `record.length_mb` 从板端 record 数据通道接收 IQ，并写入 `record.output_path`。
带回调的重载适合 GUI 或命令行显示大文件录制进度：

```cpp
dfone::DfOneRecordConfig rec;
rec.channel_mask = 0x1;
rec.length_mb = 1024;
rec.output_path = "record.cs16";

dev.record_iq(rec, [](const dfone::DfOneRecordProgress &progress) {
    std::cout << progress.fraction * 100.0 << "% "
              << progress.bytes_written << "/"
              << progress.total_bytes << " bytes\n";
});
```

回调由调用 `record_iq()` 的线程同步触发。GUI 程序应在回调里只更新受保护的界面状态，
避免执行耗时操作。

#### 10.4.9 `config`

```cpp
const DfOneCaptureConfig &config() const;
```

返回 session 最近一次成功记录的配置。

#### 10.4.10 `last_error`

```cpp
const std::string &last_error() const;
```

返回最近一次失败操作的错误字符串。下一次成功操作会清空该字符串。

## 11. IQ payload 解析

每个 frame 包含 8 通道，每通道 I/Q 两个 `int16_t`：

```text
byte offset:
0   CH0_I low
1   CH0_I high
2   CH0_Q low
3   CH0_Q high
4   CH1_I low
5   CH1_I high
6   CH1_Q low
7   CH1_Q high
...
28  CH7_I low
29  CH7_I high
30  CH7_Q low
31  CH7_Q high
```

C++ 解析示例：

```cpp
std::int16_t read_i16_le(const std::uint8_t *p)
{
    const std::uint16_t u = static_cast<std::uint16_t>(p[0]) |
                            (static_cast<std::uint16_t>(p[1]) << 8U);
    return static_cast<std::int16_t>(u);
}

void read_sample(const dfone::DfOneIqCapture &cap,
                 std::size_t frame,
                 std::size_t channel,
                 std::int16_t &i,
                 std::int16_t &q)
{
    const std::size_t base = frame * cap.bytes_per_frame + channel * 4;
    i = read_i16_le(cap.payload.data() + base);
    q = read_i16_le(cap.payload.data() + base + 2);
}
```

建议在解析前检查：

```cpp
cap.sample_format == dfone::DfOneIqSampleFormat::kCs16LeInterleavedChannels
cap.channel_count == dfone::kDfOneApiChannelCount
cap.bytes_per_frame == dfone::kDfOneApiBytesPerFrame
cap.payload.size() >= cap.frames * cap.bytes_per_frame
```

## 12. 写入 `.cs16` 文件

Public API 不强制写文件。用户如果需要保存，只需把 `payload` 原样写入：

```cpp
bool write_cs16(const std::string &path, const dfone::DfOneIqCapture &capture)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char *>(capture.payload.data()),
              static_cast<std::streamsize>(capture.payload.size()));
    return static_cast<bool>(out);
}
```

文件内容没有额外头部，就是连续 CS16 frame。

## 13. 常用流程

### 13.1 一次性配置后采集

推荐便捷写法：

```cpp
dfone::DfOneDevice dev("192.168.7.2");
dev.set_sample_rate_hz(30'720'000);
dev.set_frequency_hz(2'400'000'000ULL);
dev.set_gain_db(30);

dfone::DfOneIqCapture iq = dev.get_iq(65536);
if (iq.payload.empty()) {
    // handle error
}
```

显式写法：

```cpp
dfone::DfOneSession session;
if (!session.connect(endpoint)) {
    // handle error
}
if (!session.configure(config)) {
    // handle error
}
dfone::DfOneIqCapture iq;
if (!session.capture_iq(65536, iq)) {
    // handle error
}
```

### 13.2 运行中切换频点

```cpp
if (!dev.set_frequency_hz(2'450'000'000ULL)) {
    std::cerr << dev.last_error() << "\n";
}

dfone::DfOneIqCapture iq = dev.get_iq(65536);
```

切换 RX LO 后，板端会重新同步并校准，建议等待 API 返回成功后再采集。

### 13.3 采集未补偿 IQ

```cpp
dfone::DfOneIqCapture raw = dev.get_uncorrected_iq(65536);
if (raw.payload.empty()) {
    std::cerr << dev.last_error() << "\n";
}
```

未补偿 IQ 可用于验证当前通道原始一致性。普通业务建议使用 `capture_iq()`。

### 13.4 使用外部参考时钟

```cpp
dfone::DfOneCaptureConfig config;
config.reference_clock = dfone::DfOneReferenceClock::kExternal;
config.sample_rate_hz = 30'720'000;
config.rx_lo_hz = 2'400'000'000ULL;
config.rx_gain_db = 30;

session.configure(config);
```

如果外部参考时钟无法锁定，请检查参考输入、线缆和板端启动日志。

## 14. 请求帧数和实际帧数

`capture_iq(frames, out)` 的 `frames` 是请求值。实际返回值以 `out.frames` 为准。

原因：

- 板端会限制最大 DMA buffer，目前单次 IQ payload 最大约 4 MiB。
- 每 frame 32 字节。
- 因此单次最大 frame 数约为：

```text
4 MiB / 32 bytes = 131072 frames
```

如果请求超过板端限制，板端会截断到可采集长度。用户程序应使用：

```cpp
capture.frames
capture.payload.size()
```

而不是假设一定等于请求值。

## 15. 线程模型

`DfOneSession` 的接口是阻塞式同步调用。

建议：

- 简单 CLI 可直接在主线程调用。
- GUI 程序应把 `connect()`、`configure()`、`capture_iq()` 放入 worker thread，避免界面卡顿。
- 不要从多个线程同时调用同一个 `DfOneSession` 实例。
- 如果需要并发访问，应用层自行加锁，或者每个设备连接独占一个 session。

## 16. 错误处理

所有主要操作返回 `bool`：

```cpp
if (!session.capture_iq(frames, capture)) {
    std::cerr << session.last_error() << "\n";
}
```

常见错误字符串：

| 错误 | 含义 |
| --- | --- |
| `not connected` | 调用配置或采集前没有连接 |
| `connect failed` | command/data socket 连接失败 |
| `apply capture config failed` | 下发配置失败 |
| `set RX LO failed` | 设置 RX LO 失败 |
| `set RX gain failed` | 设置 RX gain 失败 |
| `set sample rate failed` | 设置采样率失败 |
| `capture calibrated IQ failed` | 校准后 IQ 采集失败 |
| `capture uncorrected IQ failed` | 未补偿 IQ 采集失败 |
| `independent AD9361 mode is not implemented` | 独立 AD9361 模式尚未实现 |

底层 transport 还会向 stderr/log 输出更具体的 socket 或协议错误。

## 17. 参数建议

常用默认值：

| 参数 | 推荐初值 |
| --- | --- |
| Device IP | `192.168.7.2` |
| Command port | `49208` |
| Data port | `49209` |
| Sample rate | `30.72 MSPS` |
| RX LO | `2400 MHz` |
| RX gain | `30 dB` |
| Frames | `65536` |

采集前请确认：

- 板端 `init_ad9361_dfone` 已运行。
- Host 能 ping 通板卡 IP。
- 板端补偿状态已准备好，否则同步 IQ 的一致性取决于板端当前默认补偿状态。
- 使用外部参考时钟时，参考源和外部时钟锁定状态正确。

## 18. Public API 范围

Public API 覆盖客户侧常用能力：

| 功能 | Public API |
| --- | --- |
| 连接设备 | 支持 |
| 配置 sample rate / RX LO / gain / ref clock | 支持 |
| 同步 IQ 采集 | 支持 |
| 未补偿 IQ 采集 | 支持 |
| 32 hex 板卡唯一标识 | 支持 |
| eth0 IP/MAC 配置 | 支持 |
| eMMC/QSPI 固件更新 | 支持 |

交付源码包只包含客户侧接口、传输层、用户 GUI/CLI 示例和构建脚本。

## 19. 维护 API

维护 API 入口：

```cpp
#include "dfone/maintenance.hpp"
```

示例：

```cpp
dfone::DfOneMaintenance maintenance("192.168.7.2");

std::string board_id;
if (maintenance.get_board_id(board_id)) {
    std::cout << "board_id=" << board_id << "\n";
}

dfone::DfOneNetworkConfig net;
net.mode = dfone::DfOneNetworkMode::kStatic;
net.address = "192.168.1.10";
net.netmask = "255.255.255.0";

std::string output;
if (!maintenance.set_network_config(net, output)) {
    std::cerr << maintenance.last_error() << "\n";
}
```

固件更新示例：

```cpp
dfone::DfOneFirmwareUpdateConfig update;
update.target = dfone::DfOneFirmwareTarget::kEmmc;
update.package_path = "dfone-emmc.frm";
update.reboot_after = true;

std::string output;
maintenance.update_firmware(
    update,
    [](const dfone::DfOneFirmwareProgress &progress) {
        std::cout << progress.percent << "% " << progress.stage << "\n";
    },
    output);
```

`get_board_id()` 返回由板端硬件身份材料派生的稳定 32 个 hex 字符字符串，
可作为客户系统里的板卡唯一标识。它不会返回原始 UID/DNA。

这部分只连接板端 firmware update TCP service，默认端口 `49312`。它只覆盖客户侧可用的板卡唯一标识、网口配置和固件更新能力。

## 20. 最小检查清单

如果无法采集 IQ，按下面顺序检查：

1. 板卡 IP 是否正确，默认 `192.168.7.2`。
2. 板端程序是否监听 `49208` 和 `49209`。
3. Host 侧 command/data port 是否与板端一致。
4. `connect()` 是否成功。
5. `configure()` 是否成功。
6. 请求 `frames` 是否过大，实际以 `capture.frames` 为准。
7. 如果只在 `capture_iq()` 失败，检查板端 DMA 和 FPGA 采集链路。
8. 如果外部参考相关失败，检查外部参考输入和板端日志。
