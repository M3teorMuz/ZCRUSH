// ============================================================================
//  PitcherDSP.h  -  ядро Pitcher (без зависимостей от JUCE, чтобы его можно
//  было тестировать отдельно).
//
//  Алгоритм: STFT фазовый вокодер с "identity phase locking"
//  (Laroche & Dolson) + независимый сдвиг формант через спектральную
//  огибающую "true envelope" (итеративный кепстр).
//
//   1. Окно Hann, перекрытие 4x (hop = N/4).
//   2. Для каждого пика спектра считаем точную частоту по разности фаз.
//   3. Каждый пик + его "зона влияния" целиком переносится на новую частоту
//      (pitch ratio), фазы внутри зоны сохраняют взаимное положение
//      => нет "фазовитости"/металла, как у наивного вокодера.
//   4. Спектральная огибающая (форманты) оценивается по true envelope и
//      переносится отдельно: gain(k) = Env(k / formant) / Env(k / pitch).
//
//  Латентность = N сэмплов (N = размер FFT).
// ============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace pitcher
{
constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline double wrapPi (double x) noexcept
{
    return x - kTwoPi * std::floor ((x + kPi) / kTwoPi);
}

// ---------------------------------------------------------------------------
//  Простой радикс-2 FFT (комплексный, на месте). inverse = true -> делит на N.
// ---------------------------------------------------------------------------
class FFT
{
public:
    explicit FFT (int sizePow2) : n (sizePow2)
    {
        cosT.resize ((size_t) n / 2);
        sinT.resize ((size_t) n / 2);
        rev.resize ((size_t) n);

        for (int i = 0; i < n / 2; ++i)
        {
            cosT[(size_t) i] = std::cos (kTwoPi * i / n);
            sinT[(size_t) i] = std::sin (kTwoPi * i / n);
        }

        int bits = 0;
        while ((1 << bits) < n) ++bits;

        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    void run (double* re, double* im, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;

            for (int i = 0; i < n; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const double wr = cosT[(size_t) (k * step)];
                    const double wi = inverse ? sinT[(size_t) (k * step)] : -sinT[(size_t) (k * step)];

                    const int a = i + k;
                    const int b = a + half;

                    const double tr = re[b] * wr - im[b] * wi;
                    const double ti = re[b] * wi + im[b] * wr;

                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }

        if (inverse)
        {
            const double s = 1.0 / n;
            for (int i = 0; i < n; ++i) { re[i] *= s; im[i] *= s; }
        }
    }

    int size() const noexcept { return n; }

private:
    int n;
    std::vector<double> cosT, sinT;
    std::vector<int> rev;
};

// ---------------------------------------------------------------------------
//  Общий интерфейс движков (Live = PSOLA, Smooth = фазовый вокодер)
// ---------------------------------------------------------------------------
class IShifter
{
public:
    virtual ~IShifter() = default;
    virtual void reset() = 0;
    virtual void setParameters (double pitchRatio, double formantRatio, bool preserve, double maxF0Hz) noexcept = 0;
    virtual void setRobot (double robotHz) noexcept = 0;
    // интонация (расширение/сжатие диапазона высоты), вибрато: только движок Live
    virtual void setCharacter (double /*intonation*/, double /*vibratoCents*/, double /*vibratoHz*/) noexcept {}
    virtual float processSample (float x) noexcept = 0;
    virtual int latencySamples() const noexcept = 0;
};

// ---------------------------------------------------------------------------
//  Один канал питч/формант шифтера (Smooth: STFT фазовый вокодер).
// ---------------------------------------------------------------------------
class ChannelShifter : public IShifter
{
public:
    ChannelShifter (int fftSizePow2, double sampleRateHz, int overlapFactor = 4)
        : N (fftSizePow2),
          H (fftSizePow2 / overlapFactor),
          half (fftSizePow2 / 2),
          sr (sampleRateHz),
          fft (fftSizePow2)
    {
        mask = N - 1;
        const size_t n  = (size_t) N;
        const size_t nh = (size_t) half + 1;

        win.resize (n);
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = 0.5 - 0.5 * std::cos (kTwoPi * i / N);

        // нормировка overlap-add (sum of w^2)
        double s = 0.0;
        for (int m = 0; m < overlapFactor; ++m)
            s += win[(size_t) ((m * H) % N)] * win[(size_t) ((m * H) % N)];
        norm = 1.0 / s;

        inBuf.assign (n, 0.0);
        outAcc.assign (n, 0.0);
        outQ.assign ((size_t) H, 0.0);
        re.assign (n, 0.0);
        im.assign (n, 0.0);
        cre.assign (n, 0.0);
        cim.assign (n, 0.0);

        mag.assign (nh, 0.0);
        ph.assign (nh, 0.0);
        lastPh.assign (nh, 0.0);
        freq.assign (nh, 0.0);
        outMag.assign (nh, 0.0);
        outPh.assign (nh, 0.0);
        synPrev.assign (nh, kNone);
        synNew.assign (nh, kNone);
        logMag.assign (nh, 0.0);
        env.assign (nh, 0.0);
        tgt.assign (nh, 0.0);
        lift.assign (nh, 0.0);
        accRe.assign (nh, 0.0);
        accIm.assign (nh, 0.0);
        phiH.assign (nh + 1, 0.0);
        peaks.reserve (nh);
        hiEdge.reserve (nh);

        setParameters (1.0, 1.0, true, 500.0);
        reset();
    }

    int latencySamples() const noexcept override { return N; }
    int fftSize() const noexcept        { return N; }

    void reset() override
    {
        std::fill (inBuf.begin(), inBuf.end(), 0.0);
        std::fill (outAcc.begin(), outAcc.end(), 0.0);
        std::fill (outQ.begin(), outQ.end(), 0.0);
        std::fill (lastPh.begin(), lastPh.end(), 0.0);
        std::fill (synPrev.begin(), synPrev.end(), kNone);
        std::fill (phiH.begin(), phiH.end(), 0.0);
        wr = 0;
        hopCount = 0;
        outIdx = 0;
    }

    // pitchRatio   : 2^(полутона/12)
    // formantRatio : 2^(полутона/12), смещение формант относительно ИСХОДНЫХ
    // preserve     : true  -> форманты остаются на месте при сдвиге питча (+ formantRatio)
    //                false -> форманты едут вместе с питчем (обычный питчшифт, * formantRatio)
    // maxF0Hz      : верхняя граница основного тона голоса (влияет на детальность огибающей)
    // robotHz > 0 -> режим "вокодер-робот": голос заменяется монотонной гребёнкой гармоник
    // на частоте robotHz, а спектральная огибающая (форманты) берётся из входного голоса.
    void setRobot (double robotHz) noexcept override { robotHz_ = robotHz; }

    void setParameters (double pitchRatio, double formantRatio, bool preserve, double maxF0Hz) noexcept override
    {
        pitch   = std::min (8.0, std::max (0.125, pitchRatio));
        formant = std::min (4.0, std::max (0.25, formantRatio));
        keepFormants = preserve;

        const int q = std::min (half / 4, std::max (8, (int) (kLifterScale * sr / std::max (60.0, maxF0Hz))));
        if (q != liftQ)
        {
            liftQ = q;
            const double flat = 0.6 * q;
            for (int n = 0; n <= half; ++n)
            {
                double w = 0.0;
                if (n <= q)
                    w = (n <= flat) ? 1.0 : 0.5 + 0.5 * std::cos (kPi * (n - flat) / (q - flat));
                lift[(size_t) n] = w;
            }
        }
    }

    inline float processSample (float x) noexcept override
    {
        const double y = outQ[(size_t) outIdx];
        ++outIdx;

        inBuf[(size_t) wr] = (double) x;
        wr = (wr + 1) & mask;

        if (++hopCount == H)
        {
            hopCount = 0;
            processFrame();
        }
        return (float) y;
    }

private:
    static constexpr double kNone = 1.0e30;

    void processFrame() noexcept
    {
        // ---- 1. анализ -----------------------------------------------------
        for (int i = 0; i < N; ++i)
        {
            re[(size_t) i] = inBuf[(size_t) ((wr + i) & mask)] * win[(size_t) i];
            im[(size_t) i] = 0.0;
        }
        fft.run (re.data(), im.data(), false);

        double maxMag = 0.0;
        for (int k = 0; k <= half; ++k)
        {
            const double a = re[(size_t) k], b = im[(size_t) k];
            const double m = std::sqrt (a * a + b * b);
            mag[(size_t) k] = m;
            ph[(size_t) k]  = std::atan2 (b, a);
            if (m > maxMag) maxMag = m;
        }

        // точная частота каждого бина (в бинах) по разности фаз
        const double binPerRad = N / (kTwoPi * H);
        for (int k = 0; k <= half; ++k)
        {
            const double expected = kTwoPi * (double) H * k / N;
            const double dev = wrapPi (ph[(size_t) k] - lastPh[(size_t) k] - expected);
            freq[(size_t) k] = k + dev * binPerRad;
            lastPh[(size_t) k] = ph[(size_t) k];
        }

        if (maxMag < 1.0e-9)
        {
            std::fill (synPrev.begin(), synPrev.end(), kNone);
            emitFrame (false);
            return;
        }

        // ---- 2. пики и зоны влияния ----------------------------------------
        peaks.clear();
        const double thr = maxMag * 1.0e-5;
        for (int k = 1; k < half; ++k)
        {
            const double m = mag[(size_t) k];
            if (m > thr
                && m >= mag[(size_t) k - 1] && m > mag[(size_t) k + 1]
                && (k < 2 || m >= mag[(size_t) k - 2])
                && (k + 2 > half || m > mag[(size_t) k + 2]))
                peaks.push_back (k);
        }

        if (peaks.empty())
        {
            std::fill (synPrev.begin(), synPrev.end(), kNone);
            emitFrame (false);
            return;
        }

        const int np = (int) peaks.size();
        hiEdge.resize ((size_t) np);
        for (int i = 0; i < np; ++i)
        {
            if (i == np - 1) { hiEdge[(size_t) i] = half; break; }

            int best = peaks[(size_t) i] + 1;
            double bm = mag[(size_t) best];
            for (int k = peaks[(size_t) i] + 1; k < peaks[(size_t) i + 1]; ++k)
                if (mag[(size_t) k] < bm) { bm = mag[(size_t) k]; best = k; }
            hiEdge[(size_t) i] = best;
        }

        // ---- 2b. режим робота: гармоники фиксированной частоты + огибающая голоса
        if (robotHz_ > 0.0)
        {
            std::fill (synPrev.begin(), synPrev.end(), kNone);
            computeEnvelope (maxMag);
            synthRobot();
            emitFrame (true);
            return;
        }

        // ---- 3. сдвиг питча с фазовой синхронизацией -----------------------
        std::fill (outMag.begin(), outMag.end(), 0.0);
        std::fill (outPh.begin(), outPh.end(), 0.0);
        std::fill (synNew.begin(), synNew.end(), kNone);

        const bool unity = std::abs (pitch - 1.0) < 1.0e-6;

        for (int i = 0; i < np; ++i)
        {
            const int p  = peaks[(size_t) i];
            const int lo = (i == 0) ? 0 : hiEdge[(size_t) i - 1] + 1;
            const int hi = hiEdge[(size_t) i];

            const double f = std::min ((double) p + 1.0, std::max ((double) p - 1.0, freq[(size_t) p]));
            const double phiMeasured = ph[(size_t) p] + kPi * ((double) p - f);

            int q = p;
            double fNew = f;
            double phiS = phiMeasured;

            if (! unity)
            {
                q = (int) std::lround ((double) p * pitch);
                fNew = f * pitch;

                double prev = (q >= 0 && q <= half) ? synPrev[(size_t) q] : kNone;
                if (prev > 1.0e29) prev = phiMeasured;
                phiS = wrapPi (prev + kTwoPi * fNew * (double) H / N);
            }

            const int shift = q - p;
            const double phaseQ = phiS + kPi * (fNew - (double) q);

            for (int k = lo; k <= hi; ++k)
            {
                const int kk = k + shift;
                if (kk < 0 || kk > half) continue;

                const double m = mag[(size_t) k];
                if (m > outMag[(size_t) kk])
                {
                    outMag[(size_t) kk] = m;
                    outPh[(size_t) kk]  = wrapPi (phaseQ + ph[(size_t) k] - ph[(size_t) p]);
                    synNew[(size_t) kk] = phiS;
                }
            }
        }

        synPrev.swap (synNew);

        // ---- 4. форманты ---------------------------------------------------
        const double fr = keepFormants ? formant : formant * pitch;
        if (std::abs (std::log (fr / pitch)) > 1.0e-4)
        {
            computeEnvelope (maxMag);

            for (int k = 1; k < half; ++k)
            {
                if (outMag[(size_t) k] <= 0.0) continue;
                double g = evalEnv ((double) k / fr) - evalEnv ((double) k / pitch);
                g = std::min (kMaxLogGain, std::max (-kMaxLogGain, g));
                outMag[(size_t) k] *= std::exp (g);
            }
        }

        emitFrame (true);
    }

    static inline double sinc (double x) noexcept
    {
        if (std::abs (x) < 1.0e-9) return 1.0;
        return std::sin (kPi * x) / (kPi * x);
    }

    // Строим спектр "чистых синусов" на гармониках robotHz (с точной формой окна Hann и
    // непрерывной фазой), амплитуды берём из огибающей входного голоса.
    void synthRobot() noexcept
    {
        std::fill (accRe.begin(), accRe.end(), 0.0);
        std::fill (accIm.begin(), accIm.end(), 0.0);

        const double fbin0 = std::max (20.0, robotHz_) * (double) N / sr;
        for (int h = 1; ; ++h)
        {
            const double fb = h * fbin0;
            if (fb > (double) half - 2.5 || h > half) break;

            double& phase = phiH[(size_t) h];
            phase = wrapPi (phase + kTwoPi * fb * (double) H / N);

            const double amp = kRobotGain * std::exp (evalEnv (fb / formant)) / 0.5;
            const int k0 = (int) std::lround (fb);
            for (int k = std::max (0, k0 - 3); k <= std::min (half, k0 + 3); ++k)
            {
                const double x = (double) k - fb;
                const double R = 0.5 * sinc (x) - 0.25 * sinc (x - 1.0) - 0.25 * sinc (x + 1.0);
                const double th = phase - kPi * x;
                accRe[(size_t) k] += amp * R * std::cos (th);
                accIm[(size_t) k] += amp * R * std::sin (th);
            }
        }

        for (int k = 0; k <= half; ++k)
        {
            outMag[(size_t) k] = std::sqrt (accRe[(size_t) k] * accRe[(size_t) k] + accIm[(size_t) k] * accIm[(size_t) k]);
            outPh[(size_t) k]  = std::atan2 (accIm[(size_t) k], accRe[(size_t) k]);
        }
    }

    // Огибающая спектра (формантная структура).
    // 1) строим лог-линейную интерполяцию через вершины гармоник (peak envelope) -
    //    она точно проходит через гармоники голоса и не "проваливается" между ними;
    // 2) слегка сглаживаем кепстром, чтобы огибающая была устойчива на шумах.
    void computeEnvelope (double maxMag) noexcept
    {
        const double eps = std::max (1.0e-12, maxMag * 1.0e-6);
        const int np = (int) peaks.size();

        for (int k = 0; k <= half; ++k)
            logMag[(size_t) k] = std::log (mag[(size_t) k] + eps);

        if (np >= 2)
        {
            int k = 0;
            for (int i = 0; i < np; ++i)
            {
                const int p = peaks[(size_t) i];
                const double lp = logMag[(size_t) p];
                if (i == 0)
                {
                    for (; k <= p; ++k) tgt[(size_t) k] = lp;
                }
                else
                {
                    const int p0 = peaks[(size_t) i - 1];
                    const double l0 = logMag[(size_t) p0];
                    for (; k <= p; ++k)
                        tgt[(size_t) k] = l0 + (lp - l0) * (double) (k - p0) / (double) (p - p0);
                }
            }
            for (; k <= half; ++k) tgt[(size_t) k] = logMag[(size_t) peaks[(size_t) np - 1]];
        }
        else
        {
            // один пик (чистый тон): огибающая плоская -> коррекция формант не искажает звук
            const double lp = logMag[(size_t) peaks[0]];
            for (int k = 0; k <= half; ++k) tgt[(size_t) k] = lp;
        }

        for (int k = 0; k <= half; ++k)
        {
            cre[(size_t) k] = tgt[(size_t) k];
            cim[(size_t) k] = 0.0;
        }
        for (int k = 1; k < half; ++k)
        {
            cre[(size_t) (N - k)] = tgt[(size_t) k];
            cim[(size_t) (N - k)] = 0.0;
        }

        fft.run (cre.data(), cim.data(), true);   // кепстр

        for (int n = 0; n < N; ++n)
        {
            const int nn = std::min (n, N - n);
            cre[(size_t) n] *= (nn <= liftQ) ? lift[(size_t) nn] : 0.0;
            cim[(size_t) n] = 0.0;
        }

        fft.run (cre.data(), cim.data(), false);  // сглаженный лог-спектр

        for (int k = 0; k <= half; ++k)
            env[(size_t) k] = cre[(size_t) k];
    }

    inline double evalEnv (double x) const noexcept
    {
        if (x <= 0.0)          return env[0];
        if (x >= (double) half) return env[(size_t) half];
        const int i = (int) x;
        const double fr = x - i;
        return env[(size_t) i] * (1.0 - fr) + env[(size_t) i + 1] * fr;
    }

    void emitFrame (bool hasSpectrum) noexcept
    {
        if (hasSpectrum)
        {
            for (int k = 0; k <= half; ++k)
            {
                re[(size_t) k] = outMag[(size_t) k] * std::cos (outPh[(size_t) k]);
                im[(size_t) k] = outMag[(size_t) k] * std::sin (outPh[(size_t) k]);
            }
            im[0] = 0.0;
            im[(size_t) half] = 0.0;
            for (int k = 1; k < half; ++k)
            {
                re[(size_t) (N - k)] =  re[(size_t) k];
                im[(size_t) (N - k)] = -im[(size_t) k];
            }

            fft.run (re.data(), im.data(), true);

            for (int i = 0; i < N; ++i)
                outAcc[(size_t) i] += re[(size_t) i] * win[(size_t) i] * norm;
        }

        std::memcpy (outQ.data(), outAcc.data(), sizeof (double) * (size_t) H);
        std::memmove (outAcc.data(), outAcc.data() + H, sizeof (double) * (size_t) (N - H));
        std::fill (outAcc.begin() + (N - H), outAcc.end(), 0.0);
        outIdx = 0;
    }

    static constexpr double kMaxLogGain    = 3.5;   // ~ +-30 dB
    static constexpr double kRobotGain     = 1.8;   // выравнивание громкости робота
#ifndef PITCHER_LIFTER_SCALE
#define PITCHER_LIFTER_SCALE 2.0
#endif
    static constexpr double kLifterScale   = PITCHER_LIFTER_SCALE;

    const int N, H, half;
    int mask = 0;
    double sr;
    FFT fft;

    double pitch = 1.0, formant = 1.0, robotHz_ = 0.0;
    bool keepFormants = true;
    int liftQ = -1;

    int wr = 0, hopCount = 0, outIdx = 0;
    double norm = 1.0;

    std::vector<double> win, inBuf, outAcc, outQ, re, im, cre, cim;
    std::vector<double> mag, ph, lastPh, freq, outMag, outPh, synPrev, synNew, logMag, env, tgt, lift, accRe, accIm, phiH;
    std::vector<int> peaks, hiEdge;
};

} // namespace pitcher
