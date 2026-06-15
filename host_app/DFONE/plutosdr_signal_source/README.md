# PlutoSDR Signal Source

这个目录提供一个 Python PlutoSDR 发射信号源，用于 DFONE DOA 估计测试。脚本使用 pyadi-iio 的 cyclic TX buffer，启动后 Pluto 会循环发送同一段 IQ 波形，适合固定场景下比较单音、LFM、普通 OFDM 和 DroneID-like OFDM/ZC 信号对定位结果的影响。

## 安装

```bash
cd /home/wcc/vm_box/xilinx_image_builder/host_app/DFONE/plutosdr_signal_source
python3 -m pip install -r requirements.txt
```

如果系统还没有 libiio/Pluto 支持，请先确认 `iio_info -u ip:192.168.1.10` 能找到 PlutoSDR。

## 快速开始

单音，载波为 2.4 GHz，基带偏移 100 kHz：

```bash
python3 pluto_signal_source.py \
  --uri ip:192.168.1.10 \
  --mode tone \
  --freq-mhz 2400 \
  --sample-rate-hz 2000000 \
  --tone-offset-hz 100000 \
  --gain-db -30
```

LFM/chirp，围绕载波做 1 MHz 扫频：

```bash
python3 pluto_signal_source.py \
  --uri ip:192.168.1.10 \
  --mode lfm \
  --freq-mhz 2400 \
  --sample-rate-hz 5000000 \
  --lfm-bandwidth-hz 1000000 \
  --lfm-duration-s 0.001 \
  --lfm-repeat 8 \
  --gain-db -30
```

OFDM，256 点 FFT、96 个有效子载波、32 点循环前缀：

```bash
python3 pluto_signal_source.py \
  --uri ip:192.168.1.10 \
  --mode ofdm \
  --freq-mhz 2400 \
  --sample-rate-hz 5000000 \
  --ofdm-fft-size 256 \
  --ofdm-used-subcarriers 96 \
  --ofdm-cp-len 32 \
  --ofdm-symbols 32 \
  --gain-db -30
```

DroneID-like OFDM/ZC 测试信号，移植自 `zc_gen.py` 的帧结构：

```bash
python3 pluto_signal_source.py \
  --uri ip:192.168.1.10 \
  --mode droneid \
  --freq-mhz 2400 \
  --gain-db -30 \
  --scale 0.7
```

```bash
python3 pluto_signal_source.py  \
   --uri ip:192.168.1.10   \
   --mode droneid   \
   --freq-mhz 2400   \
   --droneid-frame-period-s 0.0   \
   --gain-db -0   \
   --scale 0.8

```

`droneid` 模式默认采样率为 `61.44 MSPS`、TX RF bandwidth 为 `56 MHz`，使用 15 kHz 子载波间隔、601 个有效载波、ZC root `600` 和 `147`。有效占用带宽约为：

```text
601 * 15 kHz = 9.015 MHz
```

只生成波形并检查参数、不连接 Pluto：

```bash
python3 pluto_signal_source.py --mode droneid --dry-run
```

`--duration-s 0` 表示一直发射，按 `Ctrl-C` 停止。退出时脚本会调用 `tx_destroy_buffer()` 清理 cyclic buffer。

## 参数说明

- `--freq-mhz` / `--freq-hz`：Pluto TX LO 频率。LFM 和 OFDM 都是复基带信号，实际频谱围绕这个载波展开。
- `--sample-rate-hz`：Pluto 采样率。LFM 带宽和 OFDM 占用带宽应小于采样率，建议留出余量。
- `--bandwidth-hz`：Pluto TX RF bandwidth；不填时等于采样率。
- `--gain-db`：Pluto TX hardware gain。数值越接近 0，发射越强；建议先用 `-40` 或 `-30` dB 这类较低功率开始。
- `--scale`：IQ 峰值相对 DAC 满量程的比例，默认 `0.5`。
- `--buffer-samples`：可选的 cyclic buffer 样点数。默认由具体波形决定。
- `--tone-coherent`：把单音偏移量修正到 cyclic buffer 内整数周期，减少循环边界杂散。
- `--dry-run`：只生成波形并打印峰值、RMS、PAPR，不连接 PlutoSDR。
- `--droneid-cfo-hz`：给 DroneID-like 活跃帧添加基带频偏。比如原 `zc_gen.py` 里测试过 `8000000`、`-8000000` 和 `0`。
- `--droneid-full-frame`：使用 symbol 0 到 symbol 8 的完整帧；默认使用原脚本后半段发射用的重复 `symbol_2` 短帧结构。

## DOA 评估建议

建议先保持 DFONE GUI 中的 RX LO、采样率、增益不变，只替换 Pluto 发射波形。对比时记录每种波形的主峰角度、峰值裕量、SNR 和峰宽。OFDM/LFM 的带宽更宽，如果接收链路、校准表或 DOA 算法默认假设窄带信号，可能会出现角度偏差或峰宽变大；这正是需要评估的内容。

请在屏蔽箱、线缆耦合或合规实验环境中发射，避免无意辐射到开放频段。
