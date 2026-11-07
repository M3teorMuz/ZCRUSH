"""Синтетический 'живой' голос для тестов: глоттальные импульсы с дрожанием/вибрато/глиссандо,
формантные резонаторы, смена гласных, придыхание, шипящие и паузы."""
import numpy as np
from scipy.signal import lfilter

def resonator(f, bw, sr):
    r = np.exp(-np.pi * bw / sr)
    th = 2 * np.pi * f / sr
    a = [1.0, -2 * r * np.cos(th), r * r]
    b = [1.0 - r]
    return b, a

VOWELS = {
    "a": [(730, 90), (1090, 110), (2440, 160)],
    "i": [(270, 60), (2290, 120), (3010, 180)],
    "u": [(300, 70), (870, 100), (2240, 160)],
    "e": [(530, 80), (1840, 120), (2480, 170)],
}

def glottal_source(f0_curve, sr, jitter=0.004, shimmer=0.06, seed=0):
    rng = np.random.default_rng(seed)
    n = len(f0_curve)
    jit = np.convolve(rng.standard_normal(n), np.ones(400) / 400, mode="same") * jitter * 8
    f = f0_curve * (1 + jit)
    ph = np.cumsum(f / sr)
    p = ph % 1.0
    g = np.zeros(n)
    to, tc = 0.45, 0.12
    m1 = p < to
    g[m1] = 0.5 * (1 - np.cos(np.pi * p[m1] / to))
    m2 = (p >= to) & (p < to + tc)
    g[m2] = np.cos(0.5 * np.pi * (p[m2] - to) / tc)
    src = np.diff(g, prepend=0.0) * sr / 800.0           # производная потока
    cyc = np.floor(ph).astype(int)
    amp = 1 + shimmer * np.random.default_rng(seed + 1).standard_normal(cyc.max() + 2)
    return src * amp[cyc]

def voiced_segment(f0_curve, vowel_seq, sr, seed=0, breath=0.02):
    """vowel_seq: список (начало_доля, гласная); плавные переходы между формантными наборами."""
    n = len(f0_curve)
    src = glottal_source(f0_curve, sr, seed=seed)
    src = src + breath * np.random.default_rng(seed + 2).standard_normal(n) * np.abs(src).mean()
    keys = [v for _, v in vowel_seq]
    outs = {}
    for k in set(keys):
        y = src.copy()
        for f, bw in VOWELS[k]:
            b, a = resonator(f, bw, sr)
            y = lfilter(b, a, y)
        outs[k] = y
    w = {k: np.zeros(n) for k in outs}
    starts = [int(s * n) for s, _ in vowel_seq] + [n]
    for i, (_, k) in enumerate(vowel_seq):
        w[k][starts[i]:starts[i + 1]] = 1.0
    xf = int(0.04 * sr)
    for k in w:
        w[k] = np.convolve(w[k], np.ones(xf) / xf, mode="same")
    tot = sum(w.values()) + 1e-9
    return sum(outs[k] * w[k] / tot for k in outs)

def fricative(n, sr, seed=3):
    x = np.random.default_rng(seed).standard_normal(n)
    b, a = resonator(6500, 1800, sr)
    return lfilter(b, a, x) * 0.6

def make_phrase(sr=48000, seed=0):
    """~5 c: гласные с глиссандо/вибрато, шипящий, пауза, снова гласный. Возвращает (x, f0_curve, voiced_mask)."""
    t = np.arange(int(5.0 * sr)) / sr
    f0 = np.full_like(t, 120.0)
    f0 = np.where(t < 1.5, 120 + 40 * (t / 1.5), f0)                    # 120 -> 160 глиссандо
    f0 = np.where((t >= 1.5) & (t < 2.6), 160 - 30 * ((t - 1.5) / 1.1), f0)
    f0 = np.where(t >= 2.6, 130.0, f0)
    f0 = f0 * (1 + 0.012 * np.sin(2 * np.pi * 5.5 * t))                  # вибрато
    voiced = np.ones_like(t, dtype=bool)
    v = voiced_segment(f0, [(0.0, "a"), (0.25, "i"), (0.5, "u"), (0.75, "e")], sr, seed)
    x = np.zeros_like(t)
    env = np.ones_like(t)
    # огибающая: фразы с паузами и шипящий
    gap1 = (t > 2.6) & (t < 2.9)
    fric = (t >= 2.9) & (t < 3.3)
    gap2 = (t >= 3.3) & (t < 3.5)
    env[gap1] = 0; env[gap2] = 0
    voiced[gap1 | fric | gap2] = False
    for g0 in (0.0, 3.5):
        pass
    onset = np.clip(t / 0.03, 0, 1)
    fade = np.clip((t - 3.5) / 0.03, 0, 1) * (t >= 3.5)
    x = 0.35 * v / (np.abs(v).max()) * env
    x[:int(0.03 * sr)] *= np.linspace(0, 1, int(0.03 * sr))
    fr = fricative(len(t), sr)
    x = x + 0.25 * fr * fric * np.clip(np.minimum((t - 2.9) / 0.02, (3.3 - t) / 0.02), 0, 1)
    return x, f0, voiced

def yin_f0(x, sr, t_center, fexp, win=0.04):
    """Точный оффлайн-замер f0 в окрестности ожидаемой частоты fexp (±35%)."""
    n = int(win * sr)
    i0 = int(t_center * sr) - n // 2
    if i0 < 0 or i0 + n >= len(x): return np.nan
    seg = x[i0:i0 + n] * np.hanning(n)
    if np.sqrt(np.mean(seg ** 2)) < 5e-3: return np.nan
    nf = 1 << 16
    S = np.fft.rfft(seg, nf)
    r = np.fft.irfft(np.abs(S) ** 2)[:n]
    r = r / (r[0] + 1e-12)
    lo = int(sr / (fexp * 1.35)); hi = int(sr / (fexp * 0.74))
    hi = min(hi, n - 2)
    if lo >= hi: return np.nan
    k = lo + np.argmax(r[lo:hi])
    if r[k] < 0.5: return np.nan
    a, b, c = r[k - 1], r[k], r[k + 1]
    den = a - 2 * b + c
    d = 0.5 * (a - c) / den if abs(den) > 1e-12 else 0.0
    return sr / (k + d)
