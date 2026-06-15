# DFONE Public Host SDK

This package contains the customer-facing DFONE host SDK and example
applications.

## Public Headers

```cpp
#include "dfone/session.hpp"
#include "dfone/maintenance.hpp"
```

`dfone/session.hpp` provides device connection, RF configuration, calibrated IQ
capture, uncorrected IQ capture, and IQ record commands.

`dfone/maintenance.hpp` provides customer-available board maintenance:

- read stable 32-hex board ID
- read/write persistent eth0 IP/MAC configuration
- eMMC/QSPI `.frm` firmware update with progress callback

The package does not include manufacturing-only provisioning sources, private
keys, production test tools, or internal debug tools.

## Build

```bash
cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build -DCMAKE_BUILD_TYPE=Release
cmake --build host_app/DFONE/user_gui/build -j"$(nproc)"
```

Executables:

```text
host_app/DFONE/user_gui/build/dfone_user_cli
host_app/DFONE/user_gui/build/dfone_user_gui
```

## API Documentation

- `../../README_CN.md`: Chinese out-of-box check for Ethernet
  connection, Linux builds, Linux GUI/CLI usage, customer program integration,
  and Windows GUI connection.
- `../../README.md`: English out-of-box check for Ethernet
  connection, Linux builds, Linux GUI/CLI usage, customer program integration,
  and Windows GUI connection.
- `API_USAGE.md`: quick start guide with C++ examples for capture, board ID,
  network configuration, and firmware update.
- `PUBLIC_API_MANUAL.md`: complete public API reference.
