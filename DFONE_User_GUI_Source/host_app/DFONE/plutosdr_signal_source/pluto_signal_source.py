#!/usr/bin/env python3
"""PlutoSDR cyclic TX signal source for DFONE DOA experiments."""

from __future__ import annotations

import argparse
import math
import signal
import sys
import threading
import time
from dataclasses import dataclass
from typing import Optional

import numpy as np


DAC_FULL_SCALE = 2**16


@dataclass
class WaveformInfo:
    name: str
    iq: np.ndarray
    description: str


@dataclass
class DroneIdOfdmConfig:
    fft_size: int
    used_carriers: int
    cp_len: int


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be > 0")
    return parsed


def non_negative_float(value: str) -> float:
    parsed = float(value)
    if parsed < 0.0:
        raise argparse.ArgumentTypeError("must be >= 0")
    return parsed


def resolve_tx_lo_hz(args: argparse.Namespace) -> int:
    if args.freq_hz is not None and args.freq_mhz is not None:
        raise SystemExit("use only one of --freq-hz or --freq-mhz")
    if args.freq_hz is not None:
        return int(round(args.freq_hz))
    return int(round(args.freq_mhz * 1.0e6))


def normalize_to_dac(iq: np.ndarray, scale: float) -> np.ndarray:
    if not np.isfinite(iq).all():
        raise ValueError("waveform contains NaN or Inf")
    peak = float(np.max(np.abs(iq))) if iq.size else 0.0
    if peak <= 0.0:
        raise ValueError("waveform is empty or all zeros")
    scale = min(max(scale, 0.0), 1.0)
    return (iq / peak * (DAC_FULL_SCALE * scale)).astype(np.complex64)


def apply_mode_defaults(args: argparse.Namespace) -> None:
    if args.sample_rate_hz is None:
        args.sample_rate_hz = 61_440_000 if args.mode == "droneid" else 5_000_000
    if args.bandwidth_hz is None and args.mode == "droneid":
        args.bandwidth_hz = 56_000_000


def make_tone(args: argparse.Namespace) -> WaveformInfo:
    n_samples = args.buffer_samples or 16384
    sample_rate = float(args.sample_rate_hz)
    offset_hz = float(args.tone_offset_hz)
    actual_offset_hz = offset_hz

    if args.tone_coherent:
        cycles = int(round(offset_hz * n_samples / sample_rate))
        actual_offset_hz = cycles * sample_rate / n_samples

    n = np.arange(n_samples, dtype=np.float64)
    phase = 2.0 * np.pi * actual_offset_hz * n / sample_rate
    iq = np.exp(1j * phase)
    iq = normalize_to_dac(iq, args.scale)

    desc = f"offset={actual_offset_hz:.3f} Hz, samples={n_samples}"
    if actual_offset_hz != offset_hz:
        desc += f" (requested {offset_hz:.3f} Hz)"
    elif not args.tone_coherent:
        cycles = offset_hz * n_samples / sample_rate
        if abs(cycles - round(cycles)) > 1.0e-6:
            desc += ", non-coherent cyclic boundary"

    return WaveformInfo("tone", iq, desc)


def raised_cosine_edges(n_samples: int, ramp_samples: int) -> np.ndarray:
    window = np.ones(n_samples, dtype=np.float64)
    ramp_samples = min(ramp_samples, n_samples // 2)
    if ramp_samples <= 0:
        return window
    ramp = np.sin(0.5 * np.pi * np.arange(ramp_samples, dtype=np.float64) /
                  float(ramp_samples)) ** 2
    window[:ramp_samples] = ramp
    window[-ramp_samples:] = ramp[::-1]
    return window


def make_lfm(args: argparse.Namespace) -> WaveformInfo:
    sample_rate = float(args.sample_rate_hz)
    bw_hz = float(args.lfm_bandwidth_hz)
    duration_s = float(args.lfm_duration_s)
    if bw_hz <= 0.0:
        raise ValueError("--lfm-bandwidth-hz must be > 0")
    if duration_s <= 0.0:
        raise ValueError("--lfm-duration-s must be > 0")
    if bw_hz >= sample_rate:
        print("warning: LFM bandwidth is >= sample rate; aliasing is likely", file=sys.stderr)

    frame_samples = max(2, int(round(duration_s * sample_rate)))
    t = np.arange(frame_samples, dtype=np.float64) / sample_rate
    k_hz_per_s = bw_hz / (frame_samples / sample_rate)
    phase = 2.0 * np.pi * ((-0.5 * bw_hz) * t + 0.5 * k_hz_per_s * t * t)
    frame = np.exp(1j * phase)

    ramp_samples = int(round(args.lfm_ramp_s * sample_rate))
    frame *= raised_cosine_edges(frame_samples, ramp_samples)

    repeat = max(1, args.lfm_repeat)
    iq = np.tile(frame, repeat)
    if args.buffer_samples:
        repeats_needed = int(math.ceil(args.buffer_samples / iq.size))
        iq = np.tile(iq, repeats_needed)[:args.buffer_samples]

    iq = normalize_to_dac(iq, args.scale)
    desc = (
        f"bw={bw_hz:.3f} Hz, chirp_duration={frame_samples / sample_rate:.6f} s, "
        f"chirps_in_buffer={max(1, int(round(iq.size / frame_samples)))}"
    )
    return WaveformInfo("lfm", iq, desc)


def active_ofdm_bins(nfft: int, used_subcarriers: int) -> np.ndarray:
    if used_subcarriers > nfft - 1:
        raise ValueError("--ofdm-used-subcarriers must be <= fft_size - 1")
    neg = used_subcarriers // 2
    pos = used_subcarriers - neg
    positive = np.arange(1, pos + 1, dtype=np.int64)
    negative = np.arange(nfft - neg, nfft, dtype=np.int64) if neg else np.array([], dtype=np.int64)
    return np.concatenate((negative, positive))


def make_ofdm(args: argparse.Namespace) -> WaveformInfo:
    nfft = args.ofdm_fft_size
    cp_len = args.ofdm_cp_len
    symbols = args.ofdm_symbols
    used = args.ofdm_used_subcarriers
    if cp_len >= nfft:
        raise ValueError("--ofdm-cp-len must be smaller than --ofdm-fft-size")

    rng = np.random.default_rng(args.seed)
    bins = active_ofdm_bins(nfft, used)
    frames = []
    for _ in range(symbols):
        bits_i = rng.integers(0, 2, size=bins.size)
        bits_q = rng.integers(0, 2, size=bins.size)
        qpsk = ((2.0 * bits_i - 1.0) + 1j * (2.0 * bits_q - 1.0)) / math.sqrt(2.0)
        freq = np.zeros(nfft, dtype=np.complex128)
        freq[bins] = qpsk
        time_domain = np.fft.ifft(freq) * math.sqrt(nfft)
        with_cp = np.concatenate((time_domain[-cp_len:], time_domain)) if cp_len else time_domain
        frames.append(with_cp)

    iq = np.concatenate(frames)
    if args.buffer_samples:
        repeats_needed = int(math.ceil(args.buffer_samples / iq.size))
        iq = np.tile(iq, repeats_needed)[:args.buffer_samples]

    iq = normalize_to_dac(iq, args.scale)
    occupied_hz = used / nfft * float(args.sample_rate_hz)
    desc = (
        f"nfft={nfft}, used={used}, cp={cp_len}, symbols={symbols}, "
        f"occupied_bw~{occupied_hz:.3f} Hz"
    )
    return WaveformInfo("ofdm", iq, desc)


def droneid_fft_size(sample_rate_hz: int) -> int:
    return int(round(float(sample_rate_hz) / 15.0e3))


def droneid_cp_lengths(sample_rate_hz: int) -> tuple[int, int]:
    long_cp_len = int(round(1.0 / 192000.0 * float(sample_rate_hz)))
    short_cp_len = int(round(0.0000046875 * float(sample_rate_hz)))
    return long_cp_len, short_cp_len


def zadoff_chu_sequence(root: int, seq_length: int) -> np.ndarray:
    n = np.arange(seq_length, dtype=np.float64)
    return np.exp(-1j * np.pi * float(root) * n * (n + 1.0) / float(seq_length))


def droneid_ofdm_modulate(config: DroneIdOfdmConfig, data: np.ndarray) -> np.ndarray:
    if data.size != config.used_carriers:
        raise ValueError("DroneID OFDM data size does not match used carrier count")
    freq = np.zeros(config.fft_size, dtype=np.complex128)
    offset = int(math.ceil((config.fft_size - config.used_carriers) / 2.0))
    freq[offset:offset + config.used_carriers] = data
    time_domain = np.fft.ifft(np.fft.fftshift(freq))
    return np.concatenate((time_domain[-config.cp_len:], time_domain))


def droneid_payload_symbol(config: DroneIdOfdmConfig,
                           qpsk_without_dc: np.ndarray,
                           used_carriers: int) -> np.ndarray:
    data = np.concatenate((
        qpsk_without_dc[:(used_carriers - 1) // 2],
        np.zeros(1, dtype=np.complex128),
        qpsk_without_dc[(used_carriers - 1) // 2:],
    ))
    return droneid_ofdm_modulate(config, data)


def droneid_zc_symbol(config: DroneIdOfdmConfig,
                      root: int,
                      used_carriers: int) -> np.ndarray:
    data = zadoff_chu_sequence(root, used_carriers)
    data[used_carriers // 2] = 0.0
    return droneid_ofdm_modulate(config, data)


def droneid_random_qpsk(rng: np.random.Generator, count: int) -> np.ndarray:
    constellation = np.array([1.0 + 1.0j,
                              1.0 - 1.0j,
                              -1.0 + 1.0j,
                              -1.0 - 1.0j], dtype=np.complex128) / math.sqrt(2.0)
    return rng.choice(constellation, size=count, replace=True)


def droneid_dummy_samples(rng: np.random.Generator,
                          sample_count: int,
                          variance: float) -> np.ndarray:
    if sample_count <= 0 or variance <= 0.0:
        return np.zeros(max(0, sample_count), dtype=np.complex128)
    scale = math.sqrt(variance / 2.0)
    return scale * (rng.standard_normal(sample_count) +
                    1j * rng.standard_normal(sample_count))


def apply_baseband_frequency_offset(iq: np.ndarray,
                                    sample_rate_hz: int,
                                    offset_hz: float) -> np.ndarray:
    if offset_hz == 0.0:
        return iq
    n = np.arange(iq.size, dtype=np.float64)
    return iq * np.exp(2j * np.pi * offset_hz * n / float(sample_rate_hz))


def make_droneid(args: argparse.Namespace) -> WaveformInfo:
    used_carriers = args.droneid_carriers
    sample_rate_hz = int(args.sample_rate_hz)
    fft_size = droneid_fft_size(sample_rate_hz)
    if fft_size <= used_carriers:
        raise ValueError(
            "DroneID-like waveform needs sample_rate_hz / 15 kHz > used carriers"
        )

    long_cp_len, short_cp_len = droneid_cp_lengths(sample_rate_hz)
    short_cfg = DroneIdOfdmConfig(fft_size, used_carriers, short_cp_len)
    long_cfg = DroneIdOfdmConfig(fft_size, used_carriers, long_cp_len)
    rng = np.random.default_rng(args.seed)

    payload_count = used_carriers - 1
    payloads = [droneid_random_qpsk(rng, payload_count) for _ in range(7)]
    symbol_0 = droneid_payload_symbol(long_cfg, payloads[0], used_carriers)
    symbol_1 = droneid_payload_symbol(short_cfg, payloads[1], used_carriers)
    symbol_2 = droneid_payload_symbol(short_cfg, payloads[2], used_carriers)
    symbol_4 = droneid_payload_symbol(short_cfg, payloads[3], used_carriers)
    symbol_6 = droneid_payload_symbol(short_cfg, payloads[4], used_carriers)
    symbol_7 = droneid_payload_symbol(short_cfg, payloads[5], used_carriers)
    symbol_8 = droneid_payload_symbol(long_cfg, payloads[6], used_carriers)
    zc_1 = droneid_zc_symbol(short_cfg, args.droneid_zc_root_a, used_carriers)
    zc_147 = droneid_zc_symbol(short_cfg, args.droneid_zc_root_b, used_carriers)

    if args.droneid_full_frame:
        active_frame = np.concatenate((
            symbol_0,
            symbol_1,
            symbol_2,
            zc_1,
            symbol_4,
            zc_147,
            symbol_6,
            symbol_7,
            symbol_8,
        ))
    else:
        active_frame = np.concatenate((
            symbol_2,
            symbol_2,
            symbol_2,
            zc_1,
            symbol_4,
            zc_147,
            symbol_6,
            symbol_7,
            symbol_8,
        ))

    active_frame = apply_baseband_frequency_offset(
        active_frame, sample_rate_hz, args.droneid_cfo_hz)

    dummy = droneid_dummy_samples(
        rng, args.droneid_dummy_samples, args.droneid_dummy_variance)
    tail_zeros = np.zeros(len(symbol_4), dtype=np.complex128)
    frame = np.concatenate((dummy, dummy, active_frame, tail_zeros, tail_zeros))

    gap_samples = 0
    if args.droneid_frame_period_s > 0.0:
        period_samples = int(round(args.droneid_frame_period_s * sample_rate_hz))
        if period_samples < frame.size:
            print(
                "warning: --droneid-frame-period-s is shorter than the generated frame; "
                "no extra gap was inserted",
                file=sys.stderr,
            )
        else:
            gap_samples = period_samples - frame.size
            if gap_samples:
                frame = np.concatenate((frame, np.zeros(gap_samples, dtype=np.complex128)))

    if args.droneid_repeats > 1:
        frame = np.tile(frame, args.droneid_repeats)
    if args.buffer_samples:
        repeats_needed = int(math.ceil(args.buffer_samples / frame.size))
        frame = np.tile(frame, repeats_needed)[:args.buffer_samples]

    iq = normalize_to_dac(frame, args.scale)
    occupied_hz = used_carriers * 15.0e3
    desc = (
        f"fft={fft_size}, carriers={used_carriers}, scs=15 kHz, "
        f"occupied_bw~{occupied_hz:.3f} Hz, cp_long={long_cp_len}, "
        f"cp_short={short_cp_len}, zc_roots={args.droneid_zc_root_a}/"
        f"{args.droneid_zc_root_b}, cfo={args.droneid_cfo_hz:.3f} Hz, "
        f"period={args.droneid_frame_period_s:.3f} s, gap_samples={gap_samples}"
    )
    return WaveformInfo("droneid", iq, desc)


def build_waveform(args: argparse.Namespace) -> WaveformInfo:
    if args.mode == "tone":
        return make_tone(args)
    if args.mode == "lfm":
        return make_lfm(args)
    if args.mode == "ofdm":
        return make_ofdm(args)
    if args.mode == "droneid":
        return make_droneid(args)
    raise ValueError(f"unsupported mode: {args.mode}")


def configure_pluto(args: argparse.Namespace, tx_lo_hz: int):
    try:
        import adi
    except ImportError as exc:
        raise SystemExit(
            "pyadi-iio is not installed. Try: python3 -m pip install numpy pyadi-iio"
        ) from exc

    sdr = adi.Pluto(uri=args.uri)
    sdr.sample_rate = int(args.sample_rate_hz)
    sdr.tx_lo = int(tx_lo_hz)
    sdr.tx_rf_bandwidth = int(args.bandwidth_hz or args.sample_rate_hz)
    sdr.tx_hardwaregain_chan0 = float(args.gain_db)
    sdr.tx_cyclic_buffer = True

    try:
        sdr.tx_destroy_buffer()
    except Exception:
        pass

    return sdr


def install_signal_handlers(stop_event: threading.Event) -> None:
    def _handler(signum, frame):  # noqa: ARG001
        stop_event.set()

    signal.signal(signal.SIGINT, _handler)
    signal.signal(signal.SIGTERM, _handler)


def run_tx(args: argparse.Namespace) -> int:
    tx_lo_hz = resolve_tx_lo_hz(args)
    waveform = build_waveform(args)
    peak = float(np.max(np.abs(waveform.iq)))
    rms = float(np.sqrt(np.mean(np.abs(waveform.iq) ** 2)))
    papr_db = 20.0 * math.log10(peak / rms) if rms > 0.0 else 0.0

    print(f"uri: {args.uri}")
    print(f"tx_lo_hz: {tx_lo_hz}")
    print(f"sample_rate_hz: {args.sample_rate_hz}")
    print(f"tx_rf_bandwidth_hz: {args.bandwidth_hz or args.sample_rate_hz}")
    print(f"tx_hardwaregain_chan0_db: {args.gain_db}")
    print(f"mode: {waveform.name}")
    print(f"waveform: {waveform.description}")
    print(f"buffer_samples: {waveform.iq.size}")
    print(f"peak: {peak:.1f}, rms: {rms:.1f}, papr: {papr_db:.2f} dB")
    if args.dry_run:
        print("dry run only; PlutoSDR was not configured")
        return 0

    stop_event = threading.Event()
    install_signal_handlers(stop_event)

    sdr = configure_pluto(args, tx_lo_hz)
    try:
        sdr.tx(waveform.iq)
        print("cyclic TX started")
        if args.duration_s and args.duration_s > 0.0:
            stop_event.wait(args.duration_s)
        else:
            print("press Ctrl-C to stop")
            while not stop_event.wait(0.5):
                pass
    finally:
        try:
            sdr.tx_destroy_buffer()
            print("TX buffer destroyed")
        except Exception as exc:
            print(f"warning: failed to destroy TX buffer: {exc}", file=sys.stderr)

    return 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate PlutoSDR cyclic TX waveforms for DFONE DOA testing."
    )
    parser.add_argument("--uri", default="ip:192.168.1.10", help="Pluto IIO URI")
    parser.add_argument("--mode", choices=("tone", "lfm", "ofdm", "droneid"), default="tone")
    parser.add_argument("--freq-hz", type=float, default=None, help="TX LO frequency in Hz")
    parser.add_argument("--freq-mhz", type=float, default=2400.0, help="TX LO frequency in MHz")
    parser.add_argument(
        "--sample-rate-hz",
        type=positive_int,
        default=None,
        help="TX sample rate; defaults to 61.44e6 for droneid, otherwise 5e6",
    )
    parser.add_argument("--bandwidth-hz", type=positive_int, default=None)
    parser.add_argument(
        "--gain-db",
        type=float,
        default=-30.0,
        help="Pluto TX hardware gain in dB; closer to 0 is stronger",
    )
    parser.add_argument("--scale", type=non_negative_float, default=0.5, help="DAC peak scale 0..1")
    parser.add_argument(
        "--duration-s",
        type=non_negative_float,
        default=0.0,
        help="Transmit duration; 0 means run until Ctrl-C",
    )
    parser.add_argument(
        "--buffer-samples",
        type=positive_int,
        default=0,
        help="Optional cyclic buffer length after waveform generation",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Generate waveform and print statistics without connecting to PlutoSDR",
    )

    parser.add_argument("--tone-offset-hz", type=float, default=100_000.0)
    parser.add_argument(
        "--tone-coherent",
        action="store_true",
        help="Round tone offset to an integer number of cycles in the cyclic buffer",
    )

    parser.add_argument("--lfm-bandwidth-hz", type=float, default=1_000_000.0)
    parser.add_argument("--lfm-duration-s", type=float, default=1.0e-3)
    parser.add_argument("--lfm-repeat", type=positive_int, default=4)
    parser.add_argument(
        "--lfm-ramp-s",
        type=non_negative_float,
        default=20.0e-6,
        help="Raised-cosine ramp at each chirp edge",
    )

    parser.add_argument("--ofdm-fft-size", type=positive_int, default=256)
    parser.add_argument("--ofdm-used-subcarriers", type=positive_int, default=96)
    parser.add_argument("--ofdm-cp-len", type=int, default=32)
    parser.add_argument("--ofdm-symbols", type=positive_int, default=16)
    parser.add_argument("--seed", type=int, default=1)

    parser.add_argument("--droneid-carriers", type=positive_int, default=601)
    parser.add_argument("--droneid-zc-root-a", type=positive_int, default=1)
    parser.add_argument("--droneid-zc-root-b", type=positive_int, default=147)
    parser.add_argument(
        "--droneid-cfo-hz",
        type=float,
        default=0.0,
        help="Baseband frequency offset applied to active DroneID-like frame",
    )
    parser.add_argument("--droneid-dummy-samples", type=int, default=8192)
    parser.add_argument("--droneid-dummy-variance", type=non_negative_float, default=1.0e-6)
    parser.add_argument("--droneid-repeats", type=positive_int, default=1)
    parser.add_argument(
        "--droneid-frame-period-s",
        type=non_negative_float,
        default=0.2,
        help="Cyclic DroneID/DJI-like frame period; 0 disables inserted idle gap",
    )
    parser.add_argument(
        "--droneid-full-frame",
        action="store_true",
        help="Use symbols 0..8 instead of the shorter repeated-symbol frame from zc_gen.py",
    )

    return parser


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    apply_mode_defaults(args)
    if args.scale > 1.0:
        print("warning: --scale > 1 was clipped to 1", file=sys.stderr)
    if args.ofdm_cp_len < 0:
        parser.error("--ofdm-cp-len must be >= 0")
    if args.droneid_dummy_samples < 0:
        parser.error("--droneid-dummy-samples must be >= 0")
    return run_tx(args)


if __name__ == "__main__":
    raise SystemExit(main())
