# DFONE Windows Customer CLI Example

This example shows how a customer application can link to the DFONE Windows SDK
package and build a small command-line IQ capture program.

## Files

- `main.cpp`: connects to a DFONE board, sets sample rate, RX LO, and RX gain
  with separate API calls, captures IQ, and writes a `.cs16` file.
- `CMakeLists.txt`: links against either `sdk/static` or `sdk/shared` from the
  DFONE Windows package.

## Build With the Static SDK

Open a Visual Studio developer PowerShell and run:

```powershell
cd path\to\DFONE-Windows-SDK-1.0.0-x64\examples\windows_cli_capture

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DDFONE_SDK_ROOT=..\..\sdk\static `
  -DDFONE_USE_SHARED=OFF

cmake --build build --config Release
```

Output:

```text
build\Release\dfone_customer_capture.exe
```

## Build With the Shared SDK

```powershell
cd path\to\DFONE-Windows-SDK-1.0.0-x64\examples\windows_cli_capture

cmake -S . -B build-shared -G "Visual Studio 18 2026" -A x64 `
  -DDFONE_SDK_ROOT=..\..\sdk\shared `
  -DDFONE_USE_SHARED=ON

cmake --build build-shared --config Release
```

The build copies `dfone_host.dll` next to `dfone_customer_capture.exe`.

## Run

Connect the DFONE board and make sure the PC can reach the board IP:

```powershell
ping 192.168.7.2
```

Run a capture:

```powershell
.\build\Release\dfone_customer_capture.exe --device-ip 192.168.7.2 --frames 65536 --output iq.cs16
```

The output format is CS16 little-endian interleaved by channel:

```text
CH0_I CH0_Q CH1_I CH1_Q ... CH7_I CH7_Q
```
