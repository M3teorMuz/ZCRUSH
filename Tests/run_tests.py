#!/usr/bin/env python3
"""Автотесты DSP-ядра Pitcher. Запуск: python3 Tests/run_tests.py <путь к pitcher_cli>"""
import subprocess, sys, os, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from voice_synth import make_phrase, glottal_source, resonator, VOWELS, yin_f0
from scipy.signal import lfilter

CLI = sys.argv[1] if len(sys.argv) > 1 else "/tmp/pitcher_cli"
SR = 48000
tmp = tempfile.mkdtemp()
results = []

def check(name, ok, info=""):
    results.append(ok)
    print(("PASS  " if ok else "FAIL  ") + name + ("  | " + info if info else ""))

def run(x, ps=0.0, fs=0.0, keep=1, n=4096, maxf0=500, raw=0, sr=SR):
    a, b = os.path.join(tmp, "in.f32"), os.path.join(tmp, "out.f32")
    x.astype(np.float32).tofile(a)
    subprocess.run([CLI, "process", a, b, str(sr), str(ps), str(fs), str(keep), str(n), str(maxf0), str(raw)], check=True)
    return np.fromfile(b, dtype=np.float32).astype(np.float64)

# ---- модель гласной: гармоники с амплитудами по формантной огибающей -------
FORMANTS = [(700, 110), (1250, 140), (2600, 200)]  # (частота, ширина)
def vowel_env(f):
    f = np.asarray(f, dtype=float)
    g = np.zeros_like(f)
    for fc, bw in FORMANTS:
        g += 1.0 / (1 + ((f - fc) / bw) ** 2)
    return g / 1.3 + 0.01

def vowel(f0, secs=3.0, sr=SR):
    t = np.arange(int(sr * secs)) / sr
    x = np.zeros_like(t)
    h = 1
    while h * f0 < 8000:
        x += vowel_env(h * f0) * np.cos(2 * np.pi * h * f0 * t + 0.3 * h)
        h += 1
    return 0.3 * x / np.max(np.abs(x))

def harmonic_amps(x, f0, sr=SR, fmax=4500):
    seg = x[int(0.8 * sr):int(2.4 * sr)]
    w = np.hanning(len(seg))
    nfft = 1 << 18
    S = np.abs(np.fft.rfft(seg * w, nfft))
    fr = np.fft.rfftfreq(nfft, 1 / sr)
    hs, am = [], []
    h = 1
    while h * f0 < fmax:
        c = h * f0
        m = (fr > c - f0 * 0.15) & (fr < c + f0 * 0.15)
        hs.append(c); am.append(S[m].max())
        h += 1
    return np.array(hs), np.array(am)

def env_error_db(x, f0_out, ratio_formant, pitch_ratio=1.0):
    """Ср. отклонение (дБ) измеренной огибающей от идеальной env(f/ratio_formant).
    Меряем только там, где в исходнике была энергия (гармоники синтезированы до 8 кГц)."""
    hs, am = harmonic_amps(x, f0_out, fmax=min(4500, 7000 * pitch_ratio))
    ideal = vowel_env(hs / ratio_formant)
    d = 20 * np.log10(am + 1e-12) - 20 * np.log10(ideal)
    d -= np.median(d)
    return float(np.mean(np.abs(d)))

def peak_freq(x, sr=SR):
    seg = x[int(0.7 * sr):int(1.7 * sr)]
    nfft = 1 << 18
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft))
    return np.fft.rfftfreq(nfft, 1 / sr)[np.argmax(S)]

# ---------------------------------------------------------------------------

def run_voice(x, voice, up=0.0, uf=0.0, keep=1, n=0, sr=SR, amount=1.0):
    a, b = os.path.join(tmp, "vin.f32"), os.path.join(tmp, "vout.f32")
    x.astype(np.float32).tofile(a)
    subprocess.run([CLI, "voice", a, b, str(sr), str(voice), str(up), str(uf), str(keep), str(n), str(amount)], check=True)
    return np.fromfile(b, dtype=np.float32).astype(np.float64)

def f0_comb(x, lo, hi, sr=SR):
    """Оценка основного тона по гребёнке гармоник (устойчива к формантам)."""
    seg = x[int(0.8 * sr):int(2.4 * sr)]
    nfft = 1 << 17
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft))
    df = sr / nfft
    best, bf = -1, lo
    for f in np.arange(lo, hi, 0.25):
        idx = (np.arange(1, 13) * f / df).astype(int)
        idx = idx[idx < len(S) - 3]
        sc = sum(np.log1p(S[i - 2:i + 3].max()) for i in idx)
        if sc > best: best, bf = sc, f
    return bf

print("== 1. Латентность и прозрачность при нулевых сдвигах ==")
v = vowel(140.0)
for n in (2048, 4096):
    rng = np.random.default_rng(5)
    nz = 0.3 * rng.standard_normal(SR)           # непериодический сигнал -> однозначная корреляция
    raw = run(nz, 0, 0, 1, n, raw=1)
    c = np.correlate(raw[:SR], nz, mode="full")
    lag = int(np.argmax(c) - (SR - 1))
    check(f"латентность N={n}", lag == n, f"измерено {lag} сэмплов")
    out = run(v, 0, 0, 1, n)
    a, b = v[SR // 2:-SR // 2], out[SR // 2:len(v) - SR // 2]
    snr = 10 * np.log10(np.sum(a ** 2) / np.sum((a - b) ** 2))
    check(f"прозрачность N={n} (0 st, 0 st)", snr > 40, f"SNR {snr:.1f} дБ")

print("== 2. Точность питча на чистых синусах ==")
worst = 0
t = np.arange(SR * 3) / SR
for f in (110.0, 440.0, 1500.0, 4000.0):
    for st in (-24, -12, -7, -1, 0.37, 3, 7, 12, 19, 24):
        if f * 2 ** (st / 12) > 10000: continue
        out = run(0.5 * np.sin(2 * np.pi * f * t), st, 0, 0)
        exp = f * 2 ** (st / 12)
        cents = abs(1200 * np.log2(peak_freq(out) / exp))
        worst = max(worst, cents)
check("питч-ошибка на синусах < 5 центов", worst < 5, f"худший случай {worst:.2f} цента")

print("== 3. Форманты (гласная f0=130 Гц) ==")
v = vowel(130.0)
f0 = 130.0
for st in (+5, -5, +9, -9):
    pr = 2 ** (st / 12)
    keep = run(v, st, 0, 1)
    link = run(v, st, 0, 0)
    e_keep = env_error_db(keep, f0 * pr, 1.0, pr)    # форманты должны остаться на месте
    e_link = env_error_db(link, f0 * pr, pr, pr)     # форманты должны уехать вместе с питчем
    e_wrong = env_error_db(keep, f0 * pr, pr, pr)    # проверка метрики: "неверная" гипотеза должна быть хуже
    check(f"preserve ON, питч {st:+d} st: форманты на месте", e_keep < 3.0 and e_keep < e_wrong,
          f"ошибка {e_keep:.2f} дБ (неверная гипотеза {e_wrong:.2f} дБ)")
    check(f"preserve OFF, питч {st:+d} st: форманты едут с питчем", e_link < 3.0,
          f"ошибка {e_link:.2f} дБ")
for fs in (+4, -4, +8, -8):
    fr = 2 ** (fs / 12)
    out = run(v, 0, fs, 1)
    e = env_error_db(out, f0, fr)
    check(f"только формант {fs:+d} st (питч 0)", e < 3.5, f"ошибка {e:.2f} дБ")
out = run(v, +7, -5, 1)
e = env_error_db(out, f0 * 2 ** (7 / 12), 2 ** (-5 / 12), 2 ** (7 / 12))
check("комбинация: питч +7 st, формант -5 st", e < 3.5, f"ошибка {e:.2f} дБ")

print("== 3b. Женские/высокие голоса ==")
for f0v, st in ((220.0, 5), (220.0, -7), (330.0, 4)):
    vv = vowel(f0v)
    pr = 2 ** (st / 12)
    out = run(vv, st, 0, 1, maxf0=700)
    e = env_error_db(out, f0v * pr, 1.0, pr)
    check(f"f0={f0v:.0f} Гц, питч {st:+d} st, форманты на месте", e < 2.0, f"ошибка {e:.2f} дБ")

print("== 3c. Чистота звука (нет мусора вокруг питча) ==")
t = np.arange(SR * 3) / SR
for st in (-7, 4, 12):
    out = run(0.5 * np.sin(2 * np.pi * 440 * t), st, 0, 1)
    seg = out[int(0.8 * SR):int(2.0 * SR)]
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 18)) ** 2
    fr = np.fft.rfftfreq(1 << 18, 1 / SR)
    fpk = fr[np.argmax(S)]
    near = np.abs(fr - fpk) < 25
    ratio = 10 * np.log10(S[near].sum() / S[~near].sum())
    check(f"синус 440 Гц, {st:+d} st: паразитные компоненты", ratio > 45, f"сигнал/мусор {ratio:.1f} дБ")

print("== 3d. Вибрато без щелчков ==")
t = np.arange(SR * 3) / SR
inst = 200 * (1 + 0.02 * np.sin(2 * np.pi * 5.5 * t))
x = 0.4 * np.sin(2 * np.pi * np.cumsum(inst) / SR) + 0.2 * np.sin(2 * np.pi * np.cumsum(3 * inst) / SR)
out = run(x, 5, 0, 1)[SR:2 * SR]
d2 = np.diff(out, 2)
ratio = np.max(np.abs(d2)) / np.sqrt(np.mean(d2 ** 2))
check("вибрато +5 st: нет щелчков", ratio < 6, f"пик/rms второй разности = {ratio:.2f}")

print("== 4. Разные частоты дискретизации ==")
for sr in (44100, 96000):
    t = np.arange(sr * 3) / sr
    out = run(0.5 * np.sin(2 * np.pi * 440 * t), 5, 0, 1, sr=sr)
    exp = 440 * 2 ** (5 / 12)
    seg = out[int(0.7 * sr):int(1.7 * sr)]
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 18))
    fpk = np.fft.rfftfreq(1 << 18, 1 / sr)[np.argmax(S)]
    check(f"sr={sr}: питч +5 st", abs(1200 * np.log2(fpk / exp)) < 5, f"{fpk:.1f} Гц, ожидалось {exp:.1f}")

print("== 5. Стабильность (тишина, шум, DC, импульсы, экстремальные параметры) ==")
for n in (2048, 4096):
    r = subprocess.run([CLI, "stress", str(SR), str(n), "3"], capture_output=True, text=True)
    check(f"stress N={n}", r.returncode == 0, r.stdout.strip())

print("== 6. Нагрузка ==")
for n in (2048, 4096):
    r = subprocess.run([CLI, "bench", str(SR), str(n), "5"], capture_output=True, text=True)
    pct = float(r.stdout.split("=>")[1].split("%")[0])
    check(f"CPU N={n} (1 канал)", pct < 25, r.stdout.strip().split("=>")[1].strip()[:60])

print("== 11. Движок Live (PSOLA): задержка и прозрачность ==")
D_LIVE = (int(0.032 * SR) // 64) * 64
phrase, f0c, vmask = make_phrase(SR)
voiced_rms = np.sqrt(np.mean(phrase[:int(2.5 * SR)] ** 2))
phrase = phrase / voiced_rms * 0.1
raw = run(phrase, 0, 0, 1, 0, raw=1)
onset = lambda y: int(np.argmax(np.abs(y) > 0.05 * np.abs(y).max()))
lag = onset(raw) - onset(phrase)
check("Live: латентность", lag == D_LIVE, f"{lag} сэмплов = {1000 * lag / SR:.1f} мс (Smooth 4096: {1000 * 4096 / SR:.0f} мс)")
out = run(phrase, 0, 0, 1, 0)
snr = 10 * np.log10(np.sum(phrase ** 2) / (np.sum((phrase - out[:len(phrase)]) ** 2) + 1e-30))
check("Live: при нулевых сдвигах сигнал проходит без изменений", snr > 60, f"SNR {snr:.0f} дБ")

print("== 12. Live: точность высоты на живой речи (глиссандо, вибрато, дрожание, паузы) ==")
def contour(y, ref_ratio=1.0, win=0.04):
    ts = np.arange(0.3, 2.55, 0.04); r = []
    for tc in ts:
        fi = np.interp(tc, np.arange(len(f0c)) / SR, f0c)
        r.append(yin_f0(y, SR, tc, ref_ratio * fi, win=win))
    return np.array(r)
fin = contour(phrase)
fin_long = contour(phrase, 1.0, 0.08)
for ps in (-12, -7, -3, 3, 5, 7, 12):
    ratio = 2 ** (ps / 12)
    long_win = ps <= -12                       # для 60-80 Гц нужно окно длиннее
    fo = contour(run(phrase, ps, 0, 1, 0), ratio, 0.08 if long_win else 0.04)
    ref = fin_long if long_win else fin
    ok = ~np.isnan(fo) & ~np.isnan(ref)
    e = 1200 * np.log2(fo[ok] / (ref[ok] * ratio))
    check(f"Live: сдвиг {ps:+d} st", ok.sum() > 40 and abs(np.median(e)) < 6 and np.percentile(np.abs(e), 90) < 45 and np.abs(e).max() < 80,
          f"кадров {ok.sum()}, медиана {np.median(e):+.1f} цента, p90 {np.percentile(np.abs(e), 90):.0f}, макс {np.abs(e).max():.0f}")

print("== 13. Live: качество стационарной гласной (HNR, громкость, форманты) ==")
def steady(f0v, vowel="a", dur=3.0):
    t = np.arange(int(dur * SR)) / SR
    y = glottal_source(np.full_like(t, f0v), SR, jitter=0.0, shimmer=0.0)
    for f, bw in VOWELS[vowel]:
        b, a = resonator(f, bw, SR); y = lfilter(b, a, y)
    return 0.3 * y / np.abs(y).max()
def hnr(y, f0o):
    seg = y[int(0.8 * SR):int(2.4 * SR)]
    nf = 1 << 18; S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nf)) ** 2; fr = np.fft.rfftfreq(nf, 1 / SR)
    band = (fr > 80) & (fr < 4000); h = np.round(fr / f0o); d = np.abs(fr - h * f0o)
    harm = band & (d < 0.04 * f0o) & (h >= 1)
    return 10 * np.log10(S[harm].sum() / S[band & ~harm].sum())
worst_h, worst_l = 99, 0
for f0v in (110.0, 170.0, 250.0):
    xs = steady(f0v)
    for ps, fs in ((2, 0), (5, 0), (7, 0), (12, 0), (-3, 0), (-7, 0), (-12, 0), (5, 5), (0, -6)):
        o = run(xs, ps, fs, 1, 0)
        worst_h = min(worst_h, hnr(o, f0v * 2 ** (ps / 12)))
        lv = 20 * np.log10(np.sqrt(np.mean(o[SR:2 * SR] ** 2)) / np.sqrt(np.mean(xs[SR:2 * SR] ** 2)))
        worst_l = max(worst_l, abs(lv))
check("Live: нет паразитных компонентов (HNR > 33 дБ)", worst_h > 33, f"худший HNR {worst_h:.1f} дБ (вход ~63 дБ)")
check("Live: громкость сохраняется (±1.5 дБ)", worst_l < 1.5, f"худшее отклонение {worst_l:.2f} дБ")
v = vowel(130.0); f0v = 130.0
for st in (+5, -5, +9):
    pr = 2 ** (st / 12)
    e = env_error_db(run(v, st, 0, 1, 0), f0v * pr, 1.0, pr)
    check(f"Live: форманты на месте при питче {st:+d} st", e < 3.0, f"ошибка {e:.2f} дБ")
for fs in (+4, -4, +8):
    e = env_error_db(run(v, 0, fs, 1, 0), f0v, 2 ** (fs / 12))
    check(f"Live: сдвиг формант {fs:+d} st без смены питча", e < 3.0, f"ошибка {e:.2f} дБ")
fr_in = fricative_sig = None
from voice_synth import fricative
nz = 0.2 * fricative(SR * 2, SR)
o = run(nz, 5, 4, 1, 0)
lv = 20 * np.log10(np.sqrt(np.mean(o[SR // 2:int(1.5 * SR)] ** 2)) / np.sqrt(np.mean(nz[SR // 2:int(1.5 * SR)] ** 2)))
check("Live: шипящие (шум) проходят без провалов/выбросов громкости", abs(lv) < 2.5, f"{lv:+.1f} дБ")

print("== 14. Voice Transform: 7 характеров (движок Live) ==")
names = ["Off", "Anime", "Male", "Woman", "Baby", "Robot", "Big echo", "Super pitch up"]
BASE = {1: 4.5, 2: -4.0, 3: 3.5, 4: 8.0, 6: -2.0, 7: 12.0}
xs = steady(170.0)
rms_in = np.sqrt(np.mean(xs[SR:2 * SR] ** 2))
for vi, ps in BASE.items():
    out = run_voice(xs, vi)
    exp = 170.0 * 2 ** (ps / 12)
    est = f0_comb(out, exp * 0.8, exp * 1.25)
    cents = 1200 * np.log2(est / exp)
    lvl = 20 * np.log10(np.sqrt(np.mean(out[SR:2 * SR] ** 2)) / rms_in)
    check(f"{names[vi]}: высота характера {ps:+.1f} st", abs(cents) < 30, f"f0 {est:.1f} Гц (ожидалось {exp:.1f}), {cents:+.0f} цента")
    check(f"{names[vi]}: громкость и пик в норме", -9 < lvl < 9 and np.max(np.abs(out)) < 1.5, f"{lvl:+.1f} дБ, пик {np.max(np.abs(out)):.2f}")

out0 = run_voice(phrase, 0)
snr = 10 * np.log10(np.sum(phrase ** 2) / (np.sum((phrase - out0[:len(phrase)]) ** 2) + 1e-30))
check("Off: характер выключен -> голос не меняется", snr > 60, f"SNR {snr:.0f} дБ")
out_a0 = run_voice(phrase, 1, amount=0.0)
snr = 10 * np.log10(np.sum(phrase ** 2) / (np.sum((phrase - out_a0[:len(phrase)]) ** 2) + 1e-30))
check("Anime при AMOUNT=0 -> голос не меняется", snr > 40, f"SNR {snr:.0f} дБ")

# ручки пользователя складываются с характером
out = run_voice(xs, 1, up=2.0)
exp = 170.0 * 2 ** ((4.5 + 2.0) / 12)
est = f0_comb(out, exp * 0.8, exp * 1.25)
check("Anime + ручка PITCH +2 st = итого +6.5 st", abs(1200 * np.log2(est / exp)) < 30, f"f0 {est:.1f} Гц (ожидалось {exp:.1f})")
# AMOUNT масштабирует
out = run_voice(xs, 1, amount=0.5)
exp = 170.0 * 2 ** (4.5 * 0.5 / 12)
est = f0_comb(out, exp * 0.8, exp * 1.25)
check("Anime AMOUNT 50% -> половина сдвига", abs(1200 * np.log2(est / exp)) < 30, f"f0 {est:.1f} Гц (ожидалось {exp:.1f})")

# интонация: расширение мелодики
t = np.arange(int(5 * SR)) / SR
dev = 150 * np.sin(2 * np.pi * 0.6 * t)                 # +-150 центов вокруг 140 Гц
f0m = 140 * 2 ** (dev / 1200)
src = glottal_source(f0m, SR, jitter=0.0, shimmer=0.0)
ym = src
for f, bw in VOWELS["a"]:
    b, a = resonator(f, bw, SR); ym = lfilter(b, a, ym)
ym = 0.3 * ym / np.abs(ym).max()
def contour_c(y, ratio, tcs):
    r = []
    for tc in tcs:
        fi = 140 * 2 ** (np.interp(tc, t, dev) / 1200)
        r.append(yin_f0(y, SR, tc, fi * ratio))
    return np.array(r)
tcs = np.arange(1.5, 4.5, 0.05)
fi_ = contour_c(ym, 1.0, tcs)
for vi, expo, ps in ((4, 1.5, 8.0), (2, 0.8, -4.0)):
    o = run_voice(ym, vi)
    fo = contour_c(o, 2 ** (ps / 12), tcs)
    ok = ~np.isnan(fi_) & ~np.isnan(fo)
    ci = 1200 * np.log2(fi_[ok] / np.nanmean(fi_[ok])); co = 1200 * np.log2(fo[ok] / np.nanmean(fo[ok]))
    slope = float(np.dot(ci, co) / np.dot(ci, ci))
    check(f"{names[vi]}: интонация (ширина мелодики) x{expo}", abs(slope - expo) < 0.25, f"измеренный масштаб {slope:.2f}")

# вибрато у Baby
xs2 = steady(200.0, dur=3.0)
o = run_voice(xs2, 4)
fo = np.array([yin_f0(o, SR, tc, 200 * 2 ** (8 / 12)) for tc in np.arange(0.9, 2.4, 0.02)])
fo = fo[~np.isnan(fo)]
spread = 1200 * np.log2(np.percentile(fo, 95) / np.percentile(fo, 5)) / 2
check("Baby: есть вибрато (колебание высоты)", 10 < spread < 45, f"амплитуда ~{spread:.0f} центов (задано 25)")

# робот: монотон
o = run_voice(phrase, 5)
fo = np.array([yin_f0(o, SR, tc, 110.0) for tc in np.arange(0.4, 2.4, 0.05)])
fo = fo[~np.isnan(fo)]
check("Robot: высота постоянна при глиссандо на входе", len(fo) > 30 and np.std(1200 * np.log2(fo / 110.0)) < 20 and abs(np.median(1200 * np.log2(fo / 110.0))) < 25,
      f"медиана {np.median(fo):.1f} Гц, разброс {np.std(1200 * np.log2(fo / 110.0)):.1f} цента")

# придыхание: у Anime больше ВЧ-энергии, чем у чистого сдвига
def hf_share(y):
    seg = y[SR:int(2.5 * SR)]
    S = np.abs(np.fft.rfft(seg)) ** 2; fr = np.fft.rfftfreq(len(seg), 1 / SR)
    return 10 * np.log10(S[fr > 6000].sum() / S.sum())
a_full = run_voice(xs, 1)
pitch_only = run(xs, 4.5, 4.0, 1, 0)
check("Anime: добавляет 'воздух' в верхах относительно чистого сдвига", hf_share(a_full) > hf_share(pitch_only) + 3, f"+{hf_share(a_full) - hf_share(pitch_only):.1f} дБ в области >6 кГц")

# Big echo: хвост
imp = np.zeros(SR * 3); imp[SR // 2:SR // 2 + 2400] = 0.5 * vowel(150.0, 0.05)[:2400]
out = run_voice(imp, 6)
tail = out[SR // 2 + 4096 + 20000:SR // 2 + 4096 + 80000]
check("Big echo: длинный хвост эха/реверба", np.sqrt(np.mean(tail ** 2)) > 1e-4, f"rms хвоста {20 * np.log10(np.sqrt(np.mean(tail ** 2)) + 1e-12):.1f} дБFS")
check("Big echo: хвост затухает", np.abs(out[-5000:]).max() < np.abs(out[:SR * 2]).max())

print("== 15. Стресс всех характеров и движков ==")
for n in (0, 2048, 4096):
    r = subprocess.run([CLI, "stressvoices", str(SR), str(n), "4"], capture_output=True, text=True)
    check(f"stress характеров, движок {'Live' if n == 0 else 'Smooth ' + str(n)}", r.returncode == 0, r.stdout.strip())
r = subprocess.run([CLI, "stress", str(SR), "0", "3"], capture_output=True, text=True)
check("stress Live: тишина/шум/DC/импульсы/синус", r.returncode == 0, r.stdout.strip()[:90])
r = subprocess.run([CLI, "bench", str(SR), "0", "5"], capture_output=True, text=True)
pct = float(r.stdout.split("=>")[1].split("%")[0])
check("CPU Live (1 канал)", pct < 25, f"{pct:.1f} % одного ядра")

print()
print(f"ИТОГ: {sum(results)}/{len(results)} проверок пройдено")
sys.exit(0 if all(results) else 1)
