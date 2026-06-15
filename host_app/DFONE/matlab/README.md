# DFONE MATLAB MEX

This directory provides a MATLAB wrapper for the DFONE public C++ SDK. MATLAB
calls a MEX gateway, and the MEX gateway reuses `dfone::DfOneSession` for device
configuration and IQ capture.

## Build

Run this inside MATLAB:

```matlab
cd /home/wcc/vm_box/xilinx_image_builder/host_app/DFONE/matlab
build_dfone_mex
addpath(pwd)
```

On Windows, run the same script from the delivered DFONE SDK source tree after
selecting a supported C++ compiler with:

```matlab
mex -setup C++
```

## Capture IQ

```matlab
addpath('/home/wcc/vm_box/xilinx_image_builder/host_app/DFONE/matlab')

dev = dfone.DfOne('DeviceIP', '192.168.7.2');
dev.configure(30720000, 2400000000, 30);

cap = dev.capture(65536);
iq = cap.iq;          % frames x 8 complex double
ch0 = iq(:, 1);

plot(real(ch0));
dev.close();
```

`cap.iq` is a `frames x 8` complex matrix. Columns are `CH0` through `CH7`.
The MEX layer converts the board's CS16 little-endian payload into MATLAB
complex doubles.

For raw, uncorrected IQ:

```matlab
raw = dev.captureUncorrected(65536);
```

## Notes

- Default command port: `49208`
- Default data port: `49209`
- Default device IP: `192.168.7.2`
- One IQ frame contains 8 channels, each channel is `I int16 + Q int16`.
