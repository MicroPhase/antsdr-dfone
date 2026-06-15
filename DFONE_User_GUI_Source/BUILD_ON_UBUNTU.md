# Build DFONE User GUI on Ubuntu

This source package is intended to be built on the customer's machine, so the
resulting binary links against that machine's own glibc/libstdc++.

## Ubuntu 20.04 Dependencies

```bash
sudo apt update
sudo apt install -y cmake g++ pkg-config libglfw3-dev libglew-dev libgl1-mesa-dev
```

## Build

From the extracted package root:

```bash
cmake -S host_app/DFONE/user_gui -B host_app/DFONE/user_gui/build -DCMAKE_BUILD_TYPE=Release
cmake --build host_app/DFONE/user_gui/build --target dfone_user_gui -j"$(nproc)"
```

The GUI executable will be:

```text
host_app/DFONE/user_gui/build/dfone_user_gui
```

Run it with:

```bash
./host_app/DFONE/user_gui/build/dfone_user_gui
```

If the desktop has no OpenGL driver installed, install the vendor GPU driver or
Mesa OpenGL packages and try again.
