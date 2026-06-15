# DFONE 开箱检测

本文档用于指导客户在 DFONE 通过以太网接入上位机后完成开箱检测，并说明如何在
Linux 下编译 SDK/示例、运行 GUI/CLI，以及如何编写客户程序链接 SDK。Windows
部分说明如何通过 GUI 界面连接设备。

## 1. 网络准备

DFONE 上位机程序通过 TCP 连接板端。

| 用途 | 默认值 |
| --- | --- |
| 控制命令 TCP | `49208` |
| IQ 数据 TCP | `49209` |
| 维护和固件升级 TCP | `49312` |

SDK 默认设备 IP 为 `192.168.7.2`。如果现场通过以太网 `eth0` 连接，请使用板端实际
配置的以太网地址，例如 `192.168.1.10`。

1. 将 DFONE 以太网口和上位机接入同一个网络。
2. 将上位机网口配置到与板端相同的网段。例如板端为 `192.168.1.10/24`，
   上位机可配置为 `192.168.1.100/24`。
3. 检查网络是否可达：

   ```bash
   ping 192.168.1.10
   ```

4. 如果可以 ping 通但程序连接失败，请检查上位机防火墙是否允许访问 TCP
   `49208`、`49209`、`49312`。

## 2. Linux：从源码编译依赖库

公开 C++ SDK 库为 `dfone_host`，由 `host_app/DFONE/CMakeLists.txt` 定义，公开头文件为：

```cpp
#include "dfone/session.hpp"
#include "dfone/maintenance.hpp"
```

安装基础编译工具：

```bash
sudo apt update
sudo apt install -y cmake g++
```

编译并安装静态 SDK 库：

```bash
cmake -S host_app/DFONE -B host_app/DFONE/build-linux-sdk-static \
  -DCMAKE_BUILD_TYPE=Release \
  -DDFONE_BUILD_SHARED=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/host_app/DFONE/install-linux-sdk-static"

cmake --build host_app/DFONE/build-linux-sdk-static --target install -j"$(nproc)"
```

输出：

```text
host_app/DFONE/install-linux-sdk-static/include/dfone/*.hpp
host_app/DFONE/install-linux-sdk-static/lib/libdfone_host.a
```

如果客户程序希望动态链接，也可以编译动态库：

```bash
cmake -S host_app/DFONE -B host_app/DFONE/build-linux-sdk-shared \
  -DCMAKE_BUILD_TYPE=Release \
  -DDFONE_BUILD_SHARED=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/host_app/DFONE/install-linux-sdk-shared"

cmake --build host_app/DFONE/build-linux-sdk-shared --target install -j"$(nproc)"
```

输出：

```text
host_app/DFONE/install-linux-sdk-shared/include/dfone/*.hpp
host_app/DFONE/install-linux-sdk-shared/lib/libdfone_host.so
```

## 3. Linux：从源码编译示例 example

随源码提供的示例位于 `host_app/DFONE/user_gui`。

如果需要同时编译 GUI 和命令行示例，请先安装 GUI 依赖：

```bash
sudo apt update
sudo apt install -y cmake g++ pkg-config libglfw3-dev libglew-dev libgl1-mesa-dev
```

编译：

```bash
cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build \
  -DCMAKE_BUILD_TYPE=Release

cmake --build host_app/DFONE/user_gui/build -j"$(nproc)"
```

生成程序：

```text
host_app/DFONE/user_gui/build/dfone_user_cli
host_app/DFONE/user_gui/build/dfone_user_gui
```

如果只需要命令行示例，可以不安装 GUI 依赖：

```bash
sudo apt update
sudo apt install -y cmake g++

cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build-cli \
  -DCMAKE_BUILD_TYPE=Release \
  -DDFONE_USER_BUILD_GUI=OFF

cmake --build host_app/DFONE/user_gui/build-cli --target dfone_user_cli -j"$(nproc)"
```

## 4. Linux：通过 GUI 连接设备

运行：

```bash
./host_app/DFONE/user_gui/build/dfone_user_gui
```

在左侧面板中设置：

1. `Device IP` 填写 DFONE 以太网地址，例如 `192.168.1.10`。
2. `Command Port` 保持 `49208`，除非板端服务端口已修改。
3. `Data Port` 保持 `49209`，除非板端服务端口已修改。
4. 点击 `Connect`，顶部连接状态应变为 `connected`。
5. 设置采集参数，例如：`Reference Clock = Default`，`Sample Rate MSPS = 30.72`，
   `RX LO MHz = 2400`，`RX Gain dB = 30`，`Frames = 65536`。
6. 点击 `Capture Calibrated IQ`。
7. 确认右侧面板显示采集信息、I/Q 波形、相位或频谱。

如需在上位机保存返回的 CS16 数据，勾选 `Save app-side CS16`，设置 `Output Path`，
然后再次采集。

## 5. Linux：通过命令行连接设备

执行一次校准后 IQ 采集：

```bash
./host_app/DFONE/user_gui/build/dfone_user_cli capture \
  --device-ip 192.168.1.10 \
  --sample-rate 30720000 \
  --rx-lo 2400000000 \
  --rx-gain 30 \
  --frames 65536 \
  --output iq.cs16
```

成功时输出中应包含：

```text
capture ok
kind=calibrated
frames=65536
channel_count=8
```

采集未校准 IQ，用于验证或调试：

```bash
./host_app/DFONE/user_gui/build/dfone_user_cli capture \
  --device-ip 192.168.1.10 \
  --uncorrected \
  --output iq_uncorrected.cs16
```

## 6. Linux：自己实现程序并链接 SDK

创建 `main.cpp`：

```cpp
#include "dfone/session.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char **argv)
{
    const char *device_ip = argc > 1 ? argv[1] : "192.168.1.10";

    dfone::DfOneSession dev;
    if (!dev.open(device_ip)) {
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

    std::ofstream out("my_iq.cs16", std::ios::binary);
    out.write(reinterpret_cast<const char *>(iq.payload.data()),
              static_cast<std::streamsize>(iq.payload.size()));

    std::cout << "frames=" << iq.frames
              << " channels=" << iq.channel_count
              << " bytes=" << iq.payload.size() << "\n";

    dev.close();
    return 0;
}
```

创建 `CMakeLists.txt`，直接把 DFONE 源码 SDK 加入客户工程：

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_dfone_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(/absolute/path/to/host_app/DFONE dfone_host_build)

add_executable(my_dfone_app main.cpp)
target_link_libraries(my_dfone_app PRIVATE dfone_host)
```

编译并运行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/my_dfone_app 192.168.1.10
```

如果不希望用 `add_subdirectory`，也可以链接前面安装好的静态 SDK：

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_dfone_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DFONE_SDK_ROOT "/absolute/path/to/host_app/DFONE/install-linux-sdk-static")

add_executable(my_dfone_app main.cpp)
target_include_directories(my_dfone_app PRIVATE "${DFONE_SDK_ROOT}/include")
target_link_libraries(my_dfone_app PRIVATE "${DFONE_SDK_ROOT}/lib/libdfone_host.a")
```

如果使用安装好的动态 SDK，请链接 `libdfone_host.so`，并保证运行时能找到动态库，例如：

```bash
export LD_LIBRARY_PATH=/absolute/path/to/host_app/DFONE/install-linux-sdk-shared/lib:$LD_LIBRARY_PATH
```

## 7. Windows：通过 GUI 界面连接设备

Windows 发布包中包含 GUI 程序：

```text
dist\windows\DFONE-Windows-SDK-1.0.0-x64\apps\dfone_user_gui.exe
```

如果只构建了单 exe GUI 包，生成路径通常为：

```text
host_app\DFONE\user_gui\dist\DFONE_User_GUI.exe
```

Windows GUI 开箱检测步骤：

1. 将 DFONE 以太网口接入 Windows 上位机或与上位机相同的局域网。
2. 将 Windows 以太网适配器配置到与板端相同的网段。例如板端为 `192.168.1.10`，
   上位机为 `192.168.1.100`，子网掩码为 `255.255.255.0`。
3. 打开 PowerShell 检查连通性：

   ```powershell
   ping 192.168.1.10
   ```

4. 双击运行 `dfone_user_gui.exe` 或 `DFONE_User_GUI.exe`。
5. 在 `Device` 区域设置：`Device IP = 192.168.1.10`，`Command Port = 49208`，
   `Data Port = 49209`。
6. 点击 `Connect`，状态栏应显示 `Connection: connected`。
7. 设置常用采集参数，例如：`Sample Rate MSPS = 30.72`，`RX LO MHz = 2400`，
   `RX Gain dB = 30`，`Frames = 65536`。
8. 点击 `Capture Calibrated IQ`。
9. 确认右侧 `Last Capture` 显示采集元数据，并能看到波形、相位或频谱结果。

如果 Windows Defender 防火墙弹窗，请允许 DFONE GUI 在当前以太网网络配置文件下访问网络。

## 8. 通过标准

满足以下条件即可认为开箱检测通过：

1. 上位机可以 ping 通 DFONE 以太网 IP。
2. GUI 可以进入 `connected` 状态。
3. 校准后 IQ 采集无错误返回。
4. 返回结果显示 `channel_count=8`。
5. 保存 CS16 输出后生成非空 `.cs16` 文件。

## 9. 常见问题

- `ping` 失败：检查网线、交换机、网段、IP 冲突和板端以太网地址。
- `connect failed`：检查 `49208`、`49209` 端口、防火墙，以及是否有其他程序正在占用设备。
- `capture failed`：减小 `Frames`，检查射频参数，并重新连接。
- Linux 下 GUI 无法启动：安装 OpenGL/GLFW/GLEW 依赖，或更新显卡驱动。
- 固件升级使用 TCP `49312`；如果升级成功后板端重启导致连接断开，这是正常现象。

更多 API 细节请参考 `API_USAGE.md`、`host_app/DFONE/API_USAGE.md` 和
`host_app/DFONE/PUBLIC_API_MANUAL.md`。
