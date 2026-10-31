#!/usr/bin/env python3
"""Offline tests for the stretch engine. Run from the project dir: python3 tests/run_tests.py"""
import os, subprocess, sys, tempfile
import numpy as np
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "test_engine")
SR = 48000
TMP = tempfile.mkdtemp()
fails = []


def run(x, stretch=5.0, lpf=20000.0, key=-1, scale=0, block=512):
    """x: (n,2) float32. returns (n,2) float32 output of the engine."""
    fin, fout = os.path.join(TMP, "in.raw"), os.path.join(TMP, "out.raw")
    x.astype(np.float32).tofile(fin)
    r = subprocess.run([EXE, fin, fout, str(SR), str(stretch), str(lpf), str(key), str(scale), str(block)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return np.fromfile(fout, dtype=np.float32).reshape(-1, 2)


def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + ("   " + detail if detail else ""))
    if not ok:
        fails.append(name)


def db(v):
    return 20 * np.log10(max(v, 1e-12))


def rms(x):
    return float(np.sqrt(np.mean(np.square(x.astype(np.float64)))))


rng = np.random.default_rng(1)

# ---- 1. time-stretch factor: a tone burst at t=2.0..2.2 s inside 30 s of silence ----
n = SR * 30
x = np.zeros((n, 2), np.float32)
t = np.arange(int(0.2 * SR)) / SR
burst = 0.5 * np.sin(2 * np.pi * 1000 * t) * np.hanning(len(t))
x[2 * SR:2 * SR + len(burst), :] = burst[:, None]
for S in (5.0, 10.0):
    y = run(x, stretch=S)
    env = np.sqrt(np.convolve((y[:, 0] ** 2 + y[:, 1] ** 2) / 2, np.ones(2400) / 2400, mode="same"))
    tt = np.arange(n) / SR
    cen = float(np.sum(tt * env ** 2) / np.sum(env ** 2))
    N = 32768 / SR
    expected = 1.5 * N + S * (2.1 - N / 2)       # derived from the engine's hop structure
    width = float(np.sqrt(np.sum((tt - cen) ** 2 * env ** 2) / np.sum(env ** 2)))
    check(f"stretch x{S:g}: energy centroid", abs(cen - expected) < 0.35,
          f"measured {cen:.2f}s expected {expected:.2f}s, spread {width:.2f}s (input burst 0.2 s)")
    check(f"stretch x{S:g}: burst is smeared (spread > 0.2*S/3)", width > 0.2 * S / 3)

# ---- 2. level preservation on sustained noise ----
n = SR * 20
noise = (rng.standard_normal((n, 2)) * 0.1).astype(np.float32)
y = run(noise, stretch=4.0)
a, b = rms(noise[SR * 3:]), rms(y[SR * 3:])
check("level: output RMS within 1.5 dB of input (noise, LPF open)", abs(db(b) - db(a)) < 1.5,
      f"in {db(a):.1f} dBFS, out {db(b):.1f} dBFS")
check("no NaN / inf", bool(np.isfinite(y).all()))

# ---- 3. low-pass ----
def band_db(y, lo, hi):
    f, P = signal.welch(y[SR * 3:, 0], SR, nperseg=16384)
    m = (f >= lo) & (f < hi)
    return 10 * np.log10(P[m].mean())

y = run(noise, stretch=4.0, lpf=1000.0)
ref = band_db(y, 300, 600)
d2k = band_db(y, 1900, 2100) - ref
d4k = band_db(y, 3800, 4200) - ref
d8k = band_db(y, 7500, 8500) - ref
check("LPF 1 kHz: ~ -24 dB at 2 kHz", -29 < d2k < -19, f"{d2k:.1f} dB")
check("LPF 1 kHz: ~ -48 dB at 4 kHz", d4k < -42, f"{d4k:.1f} dB")
check("LPF 1 kHz: passband flat (300-600 Hz vs 100-200 Hz)", abs(band_db(y, 100, 200) - ref) < 2.0)
check("LPF 1 kHz: very low at 8 kHz", d8k < -60, f"{d8k:.1f} dB")

# ---- 4. key / scale mask: C major on white noise -> energy sits on C-major pitch classes ----
pink = (rng.standard_normal((SR * 30, 2)) * 0.1).astype(np.float32)
y = run(pink, stretch=3.0, key=0, scale=0)           # key C, Major
seg = y[SR * 4:, 0].astype(np.float64)
f, P = signal.welch(seg, SR, nperseg=65536)
m = (f > 100) & (f < 4000)
midi = 69 + 12 * np.log2(f[m] / 440.0)
pc = np.mod(midi, 12.0)                               # C = 0
in_scale = np.zeros(len(pc), bool)
for s in (0, 2, 4, 5, 7, 9, 11):
    d = np.abs(pc - s); d = np.minimum(d, 12 - d)
    in_scale |= d < 0.5
out_scale = ~in_scale
ratio = 10 * np.log10(P[m][in_scale].mean() / P[m][out_scale].mean())
# half-way points between notes (d == 0.5) are the worst case; ratio is averaged over the whole gap
check("key/scale (C major): in-scale bins >= 12 dB above the rest", ratio > 12, f"{ratio:.1f} dB")
tot_in, tot_out = rms(y[SR * 4:]), rms(noise[:len(y) - SR * 4]) if False else rms(pink)
check("key/scale keeps loudness roughly constant (+-3 dB)", abs(db(tot_in) - db(tot_out)) < 3.0,
      f"in {db(tot_out):.1f}, out {db(tot_in):.1f} dBFS")
# key = A minor must be a different pattern from C major mask but similar for relative key (same notes)
y2 = run(pink, stretch=3.0, key=9, scale=1)          # A minor == same notes as C major
f, P2 = signal.welch(y2[SR * 4:, 0].astype(np.float64), SR, nperseg=65536)
ratio2 = 10 * np.log10(P2[m][in_scale].mean() / P2[m][out_scale].mean())
check("A minor uses the same notes as C major", ratio2 > 12, f"{ratio2:.1f} dB")
y3 = run(pink, stretch=3.0, key=1, scale=0)          # C# major -> C-major notes should now be attenuated
f, P3 = signal.welch(y3[SR * 4:, 0].astype(np.float64), SR, nperseg=65536)
ratio3 = 10 * np.log10(P3[m][in_scale].mean() / P3[m][out_scale].mean())
check("C# major: C-major notes no longer dominant", ratio3 < ratio - 8, f"{ratio3:.1f} dB")

# ---- 5. block-size invariance & determinism ----
a = run(noise[:SR * 8], stretch=6.0, lpf=3000.0, key=2, scale=1, block=64)
b = run(noise[:SR * 8], stretch=6.0, lpf=3000.0, key=2, scale=1, block=1000)
check("same output for block 64 and 1000", np.array_equal(a, b))

# ---- 6. hot input: finite, bounded crest factor (random phases -> noise-like peaks) ----
hot = np.clip(rng.standard_normal((SR * 10, 2)) * 1.5, -1, 1).astype(np.float32)
y = run(hot, stretch=8.0)
crest = db(float(np.abs(y).max())) - db(rms(y[SR * 3:]))
check("hot input: finite and crest factor < 16 dB", bool(np.isfinite(y).all()) and crest < 16, f"crest {crest:.1f} dB")
sc = subprocess.run([os.path.join(HERE, "test_softclip")], capture_output=True, text=True)
check("softClip: identity below knee, never above 1.0, monotonic", sc.returncode == 0, sc.stdout.strip() + sc.stderr.strip())

# ---- 7. stereo: independent random phases -> decorrelated channels from mono input ----
mono = np.repeat((rng.standard_normal((SR * 12, 1)) * 0.1).astype(np.float32), 2, axis=1)
y = run(mono, stretch=4.0)
c = float(np.corrcoef(y[SR * 3:, 0], y[SR * 3:, 1])[0, 1])
check("mono input -> wide stereo output (|corr| < 0.3)", abs(c) < 0.3, f"corr {c:.2f}")

# ---- 8. silence in -> silence out ----
y = run(np.zeros((SR * 5, 2), np.float32))
check("silence in -> silence out", float(np.abs(y).max()) == 0.0)

print()
print("ALL PASSED" if not fails else f"FAILED: {fails}")
sys.exit(1 if fails else 0)
