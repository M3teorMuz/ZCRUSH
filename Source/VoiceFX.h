// ============================================================================
//  VoiceFX.h  -  модуль "Voice Transform": 7 голосовых характеров.
//
//  Это ОТДЕЛЬНЫЙ эффект плагина, не связанный с ручками PITCH / FORMANT
//  (они остаются полностью в руках пользователя и пресетом не двигаются).
//  Характер меняет не только высоту и форманты, но и:
//    - интонацию (ширину мелодики голоса),
//    - вибрато,
//    - придыхание (воздушная шумовая составляющая, привязана к громкости голоса),
//    - тембр (многополосная коррекция: грудной резонанс, "присутствие", "воздух"),
//    - пространство (эхо + реверб) или вокодер-робот.
//  Параметр AMOUNT плавно масштабирует всё сразу (0 = исходный голос).
//  Без зависимостей от JUCE.
// ============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "PitcherDSP.h"

namespace pitcher
{
struct EqBand { double db, hz, q; };   // db = 0 -> полоса выключена

struct VoiceDef
{
    const char* name;
    const char* description;
    double pitchSt;       // базовый сдвиг высоты (полутона)
    double formantSt;     // базовый сдвиг формант (полутона)
    double intonation;    // 1 = как есть; >1 живее/шире; <1 ровнее/спокойнее
    double vibratoCents;  // глубина вибрато
    double vibratoHz;
    double breath;        // придыхание 0..1
    EqBand low;           // low shelf
    EqBand mid1;          // peaking
    EqBand mid2;          // peaking
    EqBand high;          // high shelf
    double robotHz;       // >0: робот (монотон на этой частоте)
    double echoWet;       // >0: большое эхо + реверб
};

constexpr int kNumVoices = 8;

inline const VoiceDef& voiceDef (int i)
{
    static const VoiceDef defs[kNumVoices] = {
        { "Off (clean)", "no transformation", 0, 0, 1.0, 0, 5.5, 0.0,
          { 0, 180, 0.7 }, { 0, 1000, 1 }, { 0, 3000, 1 }, { 0, 8000, 0.7 }, 0.0, 0.0 },

        { "Anime voice", "bright, young, expressive, airy", 4.5, 4.0, 1.35, 0, 5.5, 0.10,
          { -4.0, 200, 0.7 }, { 1.5, 1200, 1.0 }, { 3.0, 3300, 1.0 }, { 5.0, 8000, 0.7 }, 0.0, 0.0 },

        { "Male voice", "deep, chesty, calm", -4.0, -3.5, 0.80, 0, 5.5, 0.0,
          { 3.5, 170, 0.7 }, { 2.0, 450, 0.9 }, { -1.5, 3200, 1.0 }, { -3.0, 7000, 0.7 }, 0.0, 0.0 },

        { "Woman voice", "light, soft, slightly breathy", 3.5, 2.5, 1.15, 0, 5.5, 0.06,
          { -3.0, 180, 0.7 }, { 0.0, 1000, 1.0 }, { 1.5, 3000, 1.0 }, { 2.5, 7000, 0.7 }, 0.0, 0.0 },

        { "Baby voice", "tiny, high, wobbly", 8.0, 6.5, 1.50, 25, 7.0, 0.05,
          { -6.0, 250, 0.7 }, { 1.0, 1500, 1.0 }, { 3.0, 3500, 1.0 }, { 4.0, 8000, 0.7 }, 0.0, 0.0 },

        { "Robot voice", "monotone robot, fixed pitch 110 Hz", 0, 0, 1.0, 0, 5.5, 0.0,
          { 0, 180, 0.7 }, { 0, 1000, 1 }, { 2.0, 2500, 1.0 }, { 2.0, 6000, 0.7 }, 110.0, 0.0 },

        { "Big echo voice", "deeper voice in a huge hall", -2.0, -1.5, 0.90, 0, 5.5, 0.0,
          { 2.0, 200, 0.7 }, { 0, 1000, 1 }, { 0, 3000, 1 }, { 0, 8000, 0.7 }, 0.0, 0.7 },

        { "Super pitch up voice", "extreme +12 st, chipmunk", 12.0, 6.0, 1.20, 0, 5.5, 0.0,
          { 0, 180, 0.7 }, { 0, 1000, 1 }, { 0, 3000, 1 }, { 3.0, 7000, 0.7 }, 0.0, 0.0 },
    };
    return defs[std::max (0, std::min (kNumVoices - 1, i))];
}

// Итоговые параметры движка: база характера (с учётом AMOUNT) + ручки пользователя.
struct ResolvedShift
{
    double pitchRatio;
    double formantRatio;
    double robotHz;       // 0 = обычный режим
    double intonation;
    double vibratoCents;
    double vibratoHz;
};

inline ResolvedShift resolveVoice (int voice, double amount, double userPitchSt, double userFormantSt)
{
    const VoiceDef& v = voiceDef (voice);
    amount = std::max (0.0, std::min (1.0, amount));
    ResolvedShift r;
    r.pitchRatio   = std::pow (2.0, (v.pitchSt * amount + userPitchSt) / 12.0);
    r.formantRatio = std::pow (2.0, (v.formantSt * amount + userFormantSt) / 12.0);
    r.robotHz      = (v.robotHz > 0.0 && amount > 0.5) ? v.robotHz * std::pow (2.0, userPitchSt / 12.0) : 0.0;
    r.intonation   = 1.0 + (v.intonation - 1.0) * amount;
    r.vibratoCents = v.vibratoCents * amount;
    r.vibratoHz    = v.vibratoHz;
    return r;
}

// ---------------------------------------------------------------------------
class Biquad
{
public:
    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    void reset() { z1 = z2 = 0; }

    void setLowShelf (double sr, double f, double dB)  { shelf (sr, f, dB, true); }
    void setHighShelf (double sr, double f, double dB) { shelf (sr, f, dB, false); }

    void setPeaking (double sr, double f, double dB, double q)
    {
        if (std::abs (dB) < 1.0e-6) { setIdentity(); return; }
        const double A  = std::pow (10.0, dB / 40.0);
        const double w0 = kTwoPi * std::min (f, sr * 0.45) / sr;
        const double alpha = std::sin (w0) / (2.0 * std::max (0.1, q));
        const double cs = std::cos (w0);
        const double B0 = 1 + alpha * A, B1 = -2 * cs, B2 = 1 - alpha * A;
        const double A0 = 1 + alpha / A, A1 = -2 * cs, A2 = 1 - alpha / A;
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void shelf (double sr, double f, double dB, bool low)
    {
        if (std::abs (dB) < 1.0e-6) { setIdentity(); return; }
        const double A  = std::pow (10.0, dB / 40.0);
        const double w0 = kTwoPi * std::min (f, sr * 0.45) / sr;
        const double cs = std::cos (w0), sn = std::sin (w0);
        const double alpha = sn / std::sqrt (2.0);
        const double t = 2.0 * std::sqrt (A) * alpha;
        double B0, B1, B2, A0, A1, A2;
        if (low)
        {
            B0 = A * ((A + 1) - (A - 1) * cs + t);
            B1 = 2 * A * ((A - 1) - (A + 1) * cs);
            B2 = A * ((A + 1) - (A - 1) * cs - t);
            A0 = (A + 1) + (A - 1) * cs + t;
            A1 = -2 * ((A - 1) + (A + 1) * cs);
            A2 = (A + 1) + (A - 1) * cs - t;
        }
        else
        {
            B0 = A * ((A + 1) + (A - 1) * cs + t);
            B1 = -2 * A * ((A - 1) + (A + 1) * cs);
            B2 = A * ((A + 1) + (A - 1) * cs - t);
            A0 = (A + 1) - (A - 1) * cs + t;
            A1 = 2 * ((A - 1) - (A + 1) * cs);
            A2 = (A + 1) - (A - 1) * cs - t;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

// ---------------------------------------------------------------------------
class Echo
{
public:
    void prepare (double sr, double delaySeconds, double feedback_, double dampHz)
    {
        buf.assign ((size_t) (sr * delaySeconds) + 8, 0.0);
        delay = (int) (sr * delaySeconds);
        feedback = feedback_;
        damp = 1.0 - std::exp (-kTwoPi * dampHz / sr);
        reset();
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; lp = 0.0; }

    inline double process (double x) noexcept
    {
        const int n = (int) buf.size();
        int rp = pos - delay; if (rp < 0) rp += n;
        const double y = buf[(size_t) rp];
        lp += damp * (y - lp);
        buf[(size_t) pos] = x + feedback * lp;
        if (++pos >= n) pos = 0;
        return y;
    }

private:
    std::vector<double> buf;
    int delay = 1, pos = 0;
    double feedback = 0.5, damp = 0.5, lp = 0.0;
};

class Reverb
{
public:
    void prepare (double sr, int spread)
    {
        static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int apT[4]   = { 556, 441, 341, 225 };
        const double k = sr / 44100.0;
        for (int i = 0; i < 8; ++i) { comb[i].assign ((size_t) std::max (8, (int) ((combT[i] + spread) * k)), 0.0); cpos[i] = 0; cl[i] = 0.0; }
        for (int i = 0; i < 4; ++i) { ap[i].assign ((size_t) std::max (8, (int) ((apT[i] + spread) * k)), 0.0); apos[i] = 0; }
        feedback = 0.90;
        damp = 0.30;
    }
    void reset()
    {
        for (int i = 0; i < 8; ++i) { std::fill (comb[i].begin(), comb[i].end(), 0.0); cpos[i] = 0; cl[i] = 0.0; }
        for (int i = 0; i < 4; ++i) { std::fill (ap[i].begin(), ap[i].end(), 0.0); apos[i] = 0; }
    }

    inline double process (double x) noexcept
    {
        const double in = x * 0.015;
        double sum = 0.0;
        for (int i = 0; i < 8; ++i)
        {
            const double y = comb[i][(size_t) cpos[i]];
            cl[i] = y * (1.0 - damp) + cl[i] * damp;
            comb[i][(size_t) cpos[i]] = in + cl[i] * feedback;
            if (++cpos[i] >= (int) comb[i].size()) cpos[i] = 0;
            sum += y;
        }
        for (int i = 0; i < 4; ++i)
        {
            const double b = ap[i][(size_t) apos[i]];
            const double out = -sum + b;
            ap[i][(size_t) apos[i]] = sum + b * 0.5;
            if (++apos[i] >= (int) ap[i].size()) apos[i] = 0;
            sum = out;
        }
        return sum * 3.0;
    }

private:
    std::vector<double> comb[8], ap[4];
    int cpos[8] = {}, apos[4] = {};
    double cl[8] = {};
    double feedback = 0.9, damp = 0.3;
};

// ---------------------------------------------------------------------------
//  Окраска характера на один канал: 4-полосный тембр + придыхание + эхо/реверб.
// ---------------------------------------------------------------------------
class VoiceColor
{
public:
    VoiceColor (double sampleRate, int channelIndex) : sr (sampleRate), seed (0x9E3779B9u + (uint32_t) channelIndex * 7919u)
    {
        echo.prepare (sr, channelIndex == 0 ? 0.34 : 0.43, 0.52, 3500.0);
        reverb.prepare (sr, channelIndex == 0 ? 0 : 23);
        envCoef = 1.0 - std::exp (-1.0 / (0.01 * sr));
        hpCoef = std::exp (-kTwoPi * 3000.0 / sr);
        setVoice (0, 1.0);
    }

    void setVoice (int idx, double amount)
    {
        const VoiceDef& v = voiceDef (idx);
        amount = std::max (0.0, std::min (1.0, amount));
        low.setLowShelf (sr, v.low.hz, v.low.db * amount);
        mid1.setPeaking (sr, v.mid1.hz, v.mid1.db * amount, v.mid1.q);
        mid2.setPeaking (sr, v.mid2.hz, v.mid2.db * amount, v.mid2.q);
        high.setHighShelf (sr, v.high.hz, v.high.db * amount);
        breath = v.breath * amount;
        const double newWet = v.echoWet * amount;
        if (idx != currentVoice)
        {
            low.reset(); mid1.reset(); mid2.reset(); high.reset();
            echo.reset(); reverb.reset();
            currentVoice = idx;
        }
        echoWet = newWet;
    }

    inline float process (float xin) noexcept
    {
        double x = high.process (mid2.process (mid1.process (low.process ((double) xin))));

        if (breath > 0.0)
        {
            // шум "воздуха": высокочастотный, огибается громкостью голоса
            seed = seed * 1664525u + 1013904223u;
            const double n = ((double) (int32_t) seed) * (1.0 / 2147483648.0);
            hpState = hpCoef * (hpState + n - hpPrev);   // 1-полюсный ВЧ-фильтр
            hpPrev = n;
            env += envCoef * (std::abs (x) - env);
            x += breath * 0.7 * env * hpState;
        }

        if (echoWet > 0.0)
        {
            const double e = echo.process (x);
            const double r = reverb.process (0.5 * (x + e));
            x = x + echoWet * (0.8 * e + 1.2 * r);
        }
        return (float) x;
    }

private:
    double sr;
    uint32_t seed;
    Biquad low, mid1, mid2, high;
    Echo echo;
    Reverb reverb;
    double echoWet = 0.0, breath = 0.0, env = 0.0, envCoef = 0.01, hpCoef = 0.6, hpState = 0.0, hpPrev = 0.0;
    int currentVoice = -1;
};

} // namespace pitcher
