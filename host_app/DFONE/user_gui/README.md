# DFONE Public API User Examples

这个目录模拟客户侧程序，只通过公开 API 访问设备。

允许包含：

```cpp
#include "dfone/session.hpp"
#include "dfone/maintenance.hpp"
```

不允许包含：

```cpp
#include "dfone/internal/..."
```

## 程序

- `dfone_user_cli`
  - 命令行采集示例。
  - 支持获取校准后 IQ 或未校准 IQ。
  - `--output` 是客户程序自己把返回的 payload 写成 `.cs16`，不使用内部保存逻辑。

- `dfone_user_gui`
  - 简单 GUI 示例。
  - 支持连接、配置同步采集参数、获取校准后 IQ、获取未校准 IQ。
  - 支持客户可用的板端维护功能：读取 32 hex 板卡唯一标识，读取/写入 eth0 IP/MAC 配置，eMMC/QSPI 固件更新和进度显示。
  - 解析公开 API 返回的 CS16 payload，绘制 8 通道 I/Q 时域曲线。
  - 在客户程序本地从 IQ payload 计算并绘制通道 `CH1..CH7` 相对 `CH0` 的相位趋势。
  - 绘图交互支持滚轮缩放、左键拖动平移、双击或 `Reset View` 复位。

## 构建

Linux 需要先安装 GUI 依赖：

```bash
sudo apt install cmake g++ pkg-config libglfw3-dev libglew-dev libgl1-mesa-dev
```

```bash
cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build
cmake --build host_app/DFONE/user_gui/build -j
```

如果只需要命令行示例：

```bash
cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build -DDFONE_USER_BUILD_GUI=OFF
cmake --build host_app/DFONE/user_gui/build --target dfone_user_cli -j
```

## CLI 示例

```bash
./host_app/DFONE/user_gui/build/dfone_user_cli capture \
  --device-ip 192.168.7.2 \
  --sample-rate 30720000 \
  --rx-lo 2400000000 \
  --rx-gain 30 \
  --frames 65536 \
  --output iq.cs16
```

未校准 IQ：

```bash
./host_app/DFONE/user_gui/build/dfone_user_cli capture --uncorrected
```

## 分发打包

这个目录可以直接作为客户侧应用打包入口。`dfone_host` 会静态链接进 CLI/GUI，不需要额外分发 DFONE SDK 库文件。

### Linux

Linux 分发产物使用 AppImage。需要准备 `linuxdeploy`，可以放到任意目录并通过 `LINUXDEPLOY` 指定：

```bash
LINUXDEPLOY=/path/to/linuxdeploy-x86_64.AppImage \
  host_app/DFONE/user_gui/tools/package_appimage.sh
```

生成产物示例：

```text
host_app/DFONE/user_gui/dist/DFONE_User_GUI-x86_64.AppImage
```

如果系统已经把 `linuxdeploy` 放进 `PATH`，也可以直接执行：

```bash
host_app/DFONE/user_gui/tools/package_release.sh
```

### Windows

Windows 分发产物使用单个 `DFONE_User_GUI.exe`。推荐用 Visual Studio 2022 + vcpkg 的静态 triplet 构建：

```powershell
$env:VCPKG_ROOT = "C:\vcpkg"
.\host_app\DFONE\user_gui\tools\package_windows_single_exe.ps1
```

生成产物示例：

```text
host_app/DFONE/user_gui/dist/DFONE_User_GUI.exe
```

这个脚本使用 `x64-windows-static`，并启用静态 MSVC runtime，目标是让 GUI 只需要一个 exe 就能运行。Windows 的 OpenGL 系统库由系统提供。
