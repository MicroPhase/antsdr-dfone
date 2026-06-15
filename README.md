# DFONE Out-of-Box Check

Chinese version: [README_CN.md](README_CN.md)

This guide helps customers verify a DFONE board after it is connected to a host
PC over Ethernet. It also explains how to build the SDK and examples on Linux,
how to run the GUI/CLI examples, and how to write an application that links to
the SDK. The Windows section covers device connection through the GUI.

## 1. Network Preparation

DFONE host applications use TCP to connect to the board.

| Purpose | Default |
| --- | --- |
| Command TCP | `49208` |
| IQ data TCP | `49209` |
| Maintenance/Firmware TCP | `49312` |

The SDK default device IP is `192.168.7.2`. For Ethernet `eth0`, use the actual
board IP configured for your network, for example `192.168.1.10`.

1. Connect the DFONE Ethernet port and the host PC to the same network.
2. Configure the host PC Ethernet interface in the same subnet as the board.
   Example: board `192.168.1.10/24`, host PC `192.168.1.100/24`.
3. Check network reachability:

   ```bash
   ping 192.168.1.10
   ```

4. If ping works but the application cannot connect, check that the host
   firewall allows outbound TCP connections to ports `49208`, `49209`, and
   `49312`.

## 2. Linux: Build the SDK Library from Source

The public C++ SDK library is `dfone_host`. It is defined by
`host_app/DFONE/CMakeLists.txt` and exports these public headers:

```cpp
#include "dfone/session.hpp"
#include "dfone/maintenance.hpp"
```

Install basic build tools:

```bash
sudo apt update
sudo apt install -y cmake g++
```

Build and install the static SDK library:

```bash
cmake -S host_app/DFONE -B host_app/DFONE/build-linux-sdk-static \
  -DCMAKE_BUILD_TYPE=Release \
  -DDFONE_BUILD_SHARED=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/host_app/DFONE/install-linux-sdk-static"

cmake --build host_app/DFONE/build-linux-sdk-static --target install -j"$(nproc)"
```

Output:

```text
host_app/DFONE/install-linux-sdk-static/include/dfone/*.hpp
host_app/DFONE/install-linux-sdk-static/lib/libdfone_host.a
```

Build and install the shared SDK library if your application prefers dynamic
linking:

```bash
cmake -S host_app/DFONE -B host_app/DFONE/build-linux-sdk-shared \
  -DCMAKE_BUILD_TYPE=Release \
  -DDFONE_BUILD_SHARED=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/host_app/DFONE/install-linux-sdk-shared"

cmake --build host_app/DFONE/build-linux-sdk-shared --target install -j"$(nproc)"
```

Output:

```text
host_app/DFONE/install-linux-sdk-shared/include/dfone/*.hpp
host_app/DFONE/install-linux-sdk-shared/lib/libdfone_host.so
```

## 3. Linux: Build the Example Applications from Source

The bundled examples are in `host_app/DFONE/user_gui`.

If you need both GUI and CLI examples, install the GUI dependencies first:

```bash
sudo apt update
sudo apt install -y cmake g++ pkg-config libglfw3-dev libglew-dev libgl1-mesa-dev
```

Build:

```bash
cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build \
  -DCMAKE_BUILD_TYPE=Release

cmake --build host_app/DFONE/user_gui/build -j"$(nproc)"
```

Executables:

```text
host_app/DFONE/user_gui/build/dfone_user_cli
host_app/DFONE/user_gui/build/dfone_user_gui
```

If you only need the command-line example, the GUI dependencies are not needed:

```bash
sudo apt update
sudo apt install -y cmake g++

cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build-cli \
  -DCMAKE_BUILD_TYPE=Release \
  -DDFONE_USER_BUILD_GUI=OFF

cmake --build host_app/DFONE/user_gui/build-cli --target dfone_user_cli -j"$(nproc)"
```

## 4. Linux: Connect with the GUI

Run:

```bash
./host_app/DFONE/user_gui/build/dfone_user_gui
```

Use these fields in the left panel:

1. `Device IP`: enter the DFONE Ethernet IP, for example `192.168.1.10`.
2. `Command Port`: keep `49208` unless the board service was changed.
3. `Data Port`: keep `49209` unless the board service was changed.
4. Click `Connect`. The top status line should change to `connected`.
5. Set capture parameters, for example: `Reference Clock = Default`,
   `Sample Rate MSPS = 30.72`, `RX LO MHz = 2400`, `RX Gain dB = 30`,
   `Frames = 65536`.
6. Click `Capture Calibrated IQ`.
7. Confirm that the right panel shows capture metadata, I/Q waveforms, phase,
   or spectrum.

To save the returned CS16 payload on the host PC, enable `Save app-side CS16`,
set `Output Path`, and capture again.

## 5. Linux: Connect with the CLI

Run a calibrated IQ capture:

```bash
./host_app/DFONE/user_gui/build/dfone_user_cli capture \
  --device-ip 192.168.1.10 \
  --sample-rate 30720000 \
  --rx-lo 2400000000 \
  --rx-gain 30 \
  --frames 65536 \
  --output iq.cs16
```

Expected successful output includes:

```text
capture ok
kind=calibrated
frames=65536
channel_count=8
```

Capture uncorrected IQ for verification or debugging:

```bash
./host_app/DFONE/user_gui/build/dfone_user_cli capture \
  --device-ip 192.168.1.10 \
  --uncorrected \
  --output iq_uncorrected.cs16
```

## 6. Linux: Write Your Own Program and Link to the SDK

Create `main.cpp`:

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

Create `CMakeLists.txt` and use the SDK source tree directly:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_dfone_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(/absolute/path/to/host_app/DFONE dfone_host_build)

add_executable(my_dfone_app main.cpp)
target_link_libraries(my_dfone_app PRIVATE dfone_host)
```

Build and run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/my_dfone_app 192.168.1.10
```

If you want to link to the installed static SDK instead of using
`add_subdirectory`, use:

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

For the installed shared SDK, link `libdfone_host.so` and make sure the shared
library can be found at runtime, for example:

```bash
export LD_LIBRARY_PATH=/absolute/path/to/host_app/DFONE/install-linux-sdk-shared/lib:$LD_LIBRARY_PATH
```

## 7. Windows: Connect with the GUI

The Windows release package contains the GUI executable:

```text
dist\windows\DFONE-Windows-SDK-1.0.0-x64\apps\dfone_user_gui.exe
```

If you built only the single-executable GUI package, the output is usually:

```text
host_app\DFONE\user_gui\dist\DFONE_User_GUI.exe
```

Windows GUI check:

1. Connect the DFONE Ethernet port to the PC or to the same LAN as the PC.
2. Configure the Windows Ethernet adapter to the same subnet as the board.
   Example: board `192.168.1.10`, PC `192.168.1.100`,
   subnet mask `255.255.255.0`.
3. Open PowerShell and verify reachability:

   ```powershell
   ping 192.168.1.10
   ```

4. Double-click `dfone_user_gui.exe` or `DFONE_User_GUI.exe`.
5. In the `Device` section, set: `Device IP = 192.168.1.10`,
   `Command Port = 49208`, `Data Port = 49209`.
6. Click `Connect`. The status line should show `Connection: connected`.
7. Set normal capture parameters, for example: `Sample Rate MSPS = 30.72`,
   `RX LO MHz = 2400`, `RX Gain dB = 30`, `Frames = 65536`.
8. Click `Capture Calibrated IQ`.
9. Confirm that the right panel shows `Last Capture` metadata and waveform,
   phase, or spectrum results.

If Windows Defender Firewall prompts for network access, allow access for the
DFONE GUI on the network profile used by the Ethernet adapter.

## 8. Pass Criteria

The out-of-box check passes when all of the following are true:

1. The host PC can ping the DFONE Ethernet IP.
2. The GUI can enter `connected` state.
3. A calibrated IQ capture returns without error.
4. The returned capture reports `channel_count=8`.
5. Saving CS16 output creates a non-empty `.cs16` file.

## 9. Troubleshooting

- `ping` fails: check cable, switch, subnet, duplicate IP address, and the board
  Ethernet IP.
- `connect failed`: check ports `49208` and `49209`, firewall settings, and
  whether another program is already using the board.
- `capture failed`: reduce `Frames`, verify RF parameters, and reconnect.
- GUI does not start on Linux: install OpenGL/GLFW/GLEW packages or update the
  GPU driver.
- Firmware update uses TCP `49312`; a disconnect after a successful update and
  reboot is normal.

For more API details, see `API_USAGE.md`, `host_app/DFONE/API_USAGE.md`, and
`host_app/DFONE/PUBLIC_API_MANUAL.md`.
