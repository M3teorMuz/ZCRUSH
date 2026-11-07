// ============================================================================
//  LiveShifter.h  -  движок "Live": низкая задержка, естественный тембр голоса.
//
//  Принцип (как у голосовых питч/формант-процессоров типа AlterBoy):
//   1. Трекер основного тона (YIN) следит за высотой голоса.
//   2. По входу строятся "метки периодов" (pitch marks), выровненные по форме
//      волны (корреляция с предыдущим периодом).
//   3. PSOLA: из входа вырезаются зёрна длиной ~2 периода (окно Hann) вокруг меток
//      и складываются (overlap-add) с новым шагом T/pitch -> меняется высота,
//      а форманты (огибающая внутри зерна) остаются нетронутыми.
//   4. Форманты двигаются независимо: зерно ресэмплируется (сжимается/растягивается
//      по времени) оконным sinc-интерполятором.
//  Безголосые участки (шипящие, дыхание) обрабатываются зёрнами фиксированной длины.
//
//  Задержка фиксирована: ~32 мс (D сэмплов).
//  Диапазон голоса: от ~70 Гц.
// ============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include "PitcherDSP.h"

namespace pitcher
{
class LiveShifter : public IShifter
{
public:
    explicit LiveShifter (double sampleRate) : sr (sampleRate)
    {
        D        = std::max (512, ((int) (0.032 * sr) / 64) * 64);
        thCap    = (int) (0.40 * D);
        thCapSrc = (int) (0.55 * D);
        Tu       = std::max (32, (int) (0.005 * sr));
        Lc       = std::max (32, (int) (0.0035 * sr)) & ~1;
        trackHop = std::max (32, (int) (0.0027 * sr));
        dec      = std::max (1, (int) std::lround (sr / 12000.0));
        srD      = sr / dec;
        lvlCoef   = 1.0 - std::exp (-1.0 / (0.05 * sr));
        lvlSmooth = 1.0 - std::exp (-1.0 / (0.02 * sr));
        lvlSmoothDown = 1.0 - std::exp (-1.0 / (0.004 * sr));

        inR.assign ((size_t) kInSize, 0.0);
        acc.assign ((size_t) kOutSize, 0.0);
        wacc.assign ((size_t) kOutSize, 0.0);
        dR.assign ((size_t) kDSize, 0.0);
        prevSeg.assign ((size_t) Lc, 0.0);
        yinBuf.assign ((size_t) (kYinW + 1200), 0.0);
        dif.assign ((size_t) 1200, 0.0);
        cmnd.assign ((size_t) 1200, 0.0);

        const int sn = kSincK * kSincRes + 2;
        sincT.resize ((size_t) sn);
        for (int i = 0; i < sn; ++i)
        {
            const double y = (double) i / kSincRes;
            sincT[(size_t) i] = (y < 1.0e-9) ? 1.0 : std::sin (kPi * y) / (kPi * y);
        }
        winT.resize ((size_t) kWinRes + 2);
        for (int i = 0; i < kWinRes + 2; ++i)
            winT[(size_t) i] = 0.5 + 0.5 * std::cos (kPi * std::min (1.0, (double) i / kWinRes));

        setParameters (1.0, 1.0, true, 500.0);
        reset();
    }

    int latencySamples() const noexcept override { return D; }

    void reset() override
    {
        std::fill (inR.begin(), inR.end(), 0.0);
        std::fill (acc.begin(), acc.end(), 0.0);
        std::fill (wacc.begin(), wacc.end(), 0.0);
        std::fill (dR.begin(), dR.end(), 0.0);
        n = 0; dn = 0; decAcc = 0.0; decCnt = 0; decPrev = 0.0;
        sNext = 0.0;
        marks[0] = { 0, (double) Tu, false };
        firstId = lastId = 0;
        haveSeg = false;
        trackVoiced = false; trackT = 0.0; voicedCount = unvoicedCount = 0; prevTauD = 0.0;
        bypassMix = 1.0;
        curM = -1;
        pIn = pOut = 0.0; lvlGain = 1.0;
        haveMean = false; logMean = 0.0; lastVoicedC = 0.0;
    }

    void setRobot (double robotHz) noexcept override { robotHz_ = robotHz; }

    // intonation: 1 = как есть, >1 живее/шире мелодия, <1 ровнее; vibrato: глубина (центы) и частота
    void setCharacter (double intonation, double vibratoCents, double vibratoHz) noexcept override
    {
        intonation_ = std::min (2.0, std::max (0.3, intonation));
        vibCents = std::max (0.0, vibratoCents);
        vibHz = std::max (0.5, vibratoHz);
    }

    void setParameters (double pitchRatio, double formantRatio, bool preserve, double maxF0Hz) noexcept override
    {
        pitch   = std::min (8.0, std::max (0.125, pitchRatio));
        formant = std::min (4.0, std::max (0.25, formantRatio));
        keep    = preserve;

        const double fLow = sr / (0.45 * D);                       // предел по задержке (~69 Гц)
        minF0 = std::max (fLow, maxF0Hz / 8.0);
        maxF0 = maxF0Hz * 1.25;
    }

    float processSample (float x) noexcept override
    {
        inR[(size_t) (n & kInMask)] = (double) x;
        pushDecimator ((double) x);
        ++n;
        if (n % trackHop == 0) updateTracker();

        const long long m = n - 1 - D;
        curM = m;

        while (sNext - (double) thCap <= (double) m)
            placeGrain();

        if (m < 0) return 0.0f;

        const size_t oi = (size_t) (m & kOutMask);
        const double gain = 1.0 / std::sqrt (std::max (wacc[oi], 0.35));
        double y = acc[oi] * gain;
        acc[oi] = 0.0;
        wacc[oi] = 0.0;

        const double dry = inR[(size_t) (m & kInMask)];

        // выравнивание громкости: обработанный голос звучит так же громко, как исходный
        pIn  += lvlCoef * (dry * dry - pIn);
        pOut += lvlCoef * (y * y - pOut);
        if (pOut > 1.0e-9 && pIn > 1.0e-9)
        {
            const double gTarget = std::min (1.6, std::max (0.5, std::sqrt (pIn / pOut)));
            lvlGain += (gTarget < lvlGain ? lvlSmoothDown : lvlSmooth) * (gTarget - lvlGain);
        }
        y *= lvlGain;

        // при нулевых сдвигах - абсолютно прозрачный проход (с плавным переключением)
        const bool neutral = std::abs (pitch - 1.0) < 1.0e-6 && std::abs (effFormant() - 1.0) < 1.0e-6 && robotHz_ <= 0.0;
        const double target = neutral ? 1.0 : 0.0;
        const double step = 1.0 / (0.01 * sr);
        bypassMix += std::max (-step, std::min (step, target - bypassMix));
        y += bypassMix * (dry - y);
        return (float) y;
    }

private:
    struct Mark { long long pos; double T; bool voiced; };

    static constexpr int kInSize = 1 << 15, kInMask = kInSize - 1;
    static constexpr int kOutSize = 1 << 14, kOutMask = kOutSize - 1;
    static constexpr int kDSize = 1 << 11, kDMask = kDSize - 1;
    static constexpr int kYinW = 256;
    static constexpr int kSincK = 8, kSincRes = 256, kWinRes = 512;

    double effFormant() const noexcept
    {
        const double f = keep ? formant : formant * pitch;
        return std::min (5.0, std::max (0.2, f));
    }

    inline double xin (long long i) const noexcept { return inR[(size_t) (i & kInMask)]; }

    // ---- оконный sinc-интерполятор по таблицам --------------------------------
    inline double readSinc (double u, double fc) const noexcept
    {
        const long long i0 = (long long) std::floor (u);
        const double fr = u - (double) i0;
        double sum = 0.0;
        for (int k = -kSincK + 1; k <= kSincK; ++k)
        {
            const double x = (double) k - fr;
            const double ax = std::abs (x);
            if (ax >= (double) kSincK) continue;

            const double yv = fc * ax * kSincRes;
            const int yi = (int) yv;
            const double yf = yv - yi;
            const double sv = sincT[(size_t) yi] * (1.0 - yf) + sincT[(size_t) yi + 1] * yf;

            const double wv = ax / kSincK * kWinRes;
            const int wi = (int) wv;
            const double wf = wv - wi;
            const double wt = winT[(size_t) wi] * (1.0 - wf) + winT[(size_t) wi + 1] * wf;

            sum += fc * sv * wt * xin (i0 + k);
        }
        return sum;
    }

    // ---- прореживание для трекера --------------------------------------------
    inline void pushDecimator (double x) noexcept
    {
        decAcc += x;
        if (++decCnt >= dec)
        {
            const double v = decAcc / dec;
            decAcc = 0.0; decCnt = 0;
            dR[(size_t) (dn & kDMask)] = 0.5 * (v + decPrev);
            decPrev = v;
            ++dn;
        }
    }

    // ---- трекер основного тона (YIN) -----------------------------------------
    void updateTracker() noexcept
    {
        const int W = kYinW;
        const int tauMax = std::min (1100, std::min (W + 600, (int) (srD / minF0)));
        const int tauMin = std::max (2, (int) (srD / maxF0));
        const int L = W + tauMax + 1;

        auto setUnvoiced = [this]()
        {
            voicedCount = 0;
            if (++unvoicedCount >= 3) { trackVoiced = false; prevTauD = 0.0; }
        };

        if (dn < L + 2) { setUnvoiced(); return; }

        for (int i = 0; i < L; ++i)
            yinBuf[(size_t) i] = dR[(size_t) ((dn - L + i) & kDMask)];

        double energy = 0.0;
        for (int j = 0; j < W; ++j) { const double v = yinBuf[(size_t) (L - 1 - j)]; energy += v * v; }
        if (std::sqrt (energy / W) < 2.0e-4) { setUnvoiced(); return; }

        const double* y = yinBuf.data();
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < W; ++j)
            {
                const double dd = y[L - 1 - j] - y[L - 1 - j - tau];
                sum += dd * dd;
            }
            dif[(size_t) tau] = sum;
        }

        double run = 0.0;
        cmnd[0] = 1.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += dif[(size_t) tau];
            cmnd[(size_t) tau] = run > 1.0e-12 ? dif[(size_t) tau] * tau / run : 1.0;
        }

        // первый провал ниже порога
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.15)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            double mn = 1.0e9;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (cmnd[(size_t) tau] < mn) { mn = cmnd[(size_t) tau]; best = tau; }
            if (mn > 0.30) best = -1;
        }

        // непрерывность: не прыгаем на октаву, если рядом с прошлым периодом есть хороший минимум
        if (best >= 0 && trackVoiced && prevTauD > 0.0)
        {
            const int lo = std::max (tauMin, (int) (0.8 * prevTauD)), hi = std::min (tauMax - 1, (int) (1.25 * prevTauD) + 1);
            int bc = -1; double bm = 1.0e9;
            for (int tau = lo; tau <= hi; ++tau)
                if (cmnd[(size_t) tau] < bm) { bm = cmnd[(size_t) tau]; bc = tau; }
            const double ratio = (double) best / prevTauD;
            if (bc >= 0 && bm < 0.25 && (ratio < 0.8 || ratio > 1.25)) best = bc;
        }

        if (best < 2 || best >= tauMax) { setUnvoiced(); return; }

        const double c0 = cmnd[(size_t) best - 1], c1 = cmnd[(size_t) best], c2 = cmnd[(size_t) best + 1];
        const double den = c0 - 2.0 * c1 + c2;
        const double tauI = (double) best + (std::abs (den) > 1.0e-12 ? 0.5 * (c0 - c2) / den : 0.0);

        prevTauD = tauI;
        unvoicedCount = 0;
        ++voicedCount;
        const double T = std::min (sr / minF0, std::max (sr / maxF0, tauI * dec));
        if (trackVoiced) trackT = 0.5 * trackT + 0.5 * T;
        else if (voicedCount >= 2) { trackVoiced = true; trackT = T; }
    }

    // ---- метки периодов -------------------------------------------------------
    bool generateNextMark() noexcept
    {
        const Mark last = marks[lastId & 127];
        const bool voiced = trackVoiced;
        const double T = voiced ? trackT : (double) Tu;
        const long long pred = last.pos + std::max<long long> (1, std::llround (T));
        const int half = Lc / 2;
        const bool refine = voiced && last.voiced && haveSeg;
        const bool snap = voiced && ! refine;          // начало озвученной цепочки: прижимаем метку к пику импульса
        const int delta = refine ? std::max (3, (int) (0.2 * T)) : (snap ? (int) (0.5 * T) : 0);

        if (pred + delta + half + 1 > n - 1) return false;

        long long newPos = pred;
        if (refine)
        {
            double bestScore = -1.0e300;
            int bestS = 0;
            for (int s = -delta; s <= delta; ++s)
            {
                const long long st = pred + s - half;
                double dot = 0.0, e = 1.0e-9;
                for (int j = 0; j < Lc; ++j)
                {
                    const double v = xin (st + j);
                    dot += prevSeg[(size_t) j] * v;
                    e += v * v;
                }
                const double score = dot / std::sqrt (e);
                if (score > bestScore) { bestScore = score; bestS = s; }
            }
            newPos = pred + bestS;
        }
        if (snap)
        {
            double bv = -1.0;
            for (int s2 = -delta; s2 <= delta; ++s2)
            {
                const double v = std::abs (xin (pred + s2));
                if (v > bv) { bv = v; newPos = pred + s2; }
            }
        }
        if (newPos <= last.pos) newPos = last.pos + 1;

        if (voiced)
        {
            const long long st = newPos - half;
            for (int j = 0; j < Lc; ++j) prevSeg[(size_t) j] = xin (st + j);
            haveSeg = true;
        }
        else haveSeg = false;

        ++lastId;
        marks[lastId & 127] = { newPos, T, voiced };
        if (lastId - firstId >= 120) ++firstId;
        return true;
    }

    void ensureMarks (long long target) noexcept
    {
        int guard = 0;
        while (marks[lastId & 127].pos <= target && guard++ < 64)
            if (! generateNextMark()) break;
    }

    // ---- один PSOLA-grain -------------------------------------------------------
    void placeGrain() noexcept
    {
        const double c = sNext;
        const long long ci = (long long) std::floor (c);
        ensureMarks (ci + 1);

        int id = lastId;
        while (id > firstId && marks[id & 127].pos > ci) --id;
        Mark a = marks[id & 127];
        if (a.pos > ci) a = { ci, (double) Tu, false };

        double T = a.T;
        if (a.voiced)
        {
            int k = 0;
            while (k < 6 && id - k - 1 >= firstId && marks[(id - k - 1) & 127].voiced) ++k;
            if (k >= 3) T = (double) (a.pos - marks[(id - k) & 127].pos) / k;
        }
        T = std::max (16.0, T);

        double Tsyn;
        if (a.voiced)
        {
            if (robotHz_ > 0.0)
                Tsyn = sr / robotHz_;
            else
            {
                // медленное среднее высоты голоса (~2 с) -> интонация относительно него
                const double lf = std::log (sr / T);
                if (! haveMean || c - lastVoicedC > 1.5 * sr) { logMean = lf; haveMean = true; }
                else logMean += (1.0 - std::exp (-T / (2.0 * sr))) * (lf - logMean);
                lastVoicedC = c;

                double ratio = pitch;
                if (std::abs (intonation_ - 1.0) > 1.0e-6)
                    ratio *= std::exp ((intonation_ - 1.0) * (lf - logMean));
                if (vibCents > 0.0)
                    ratio *= std::pow (2.0, (vibCents / 1200.0) * std::sin (kTwoPi * vibHz * c / sr));
                ratio = std::min (8.0, std::max (0.125, ratio));
                Tsyn = T / ratio;
            }
        }
        else Tsyn = T;
        Tsyn = std::max (8.0, Tsyn);

        const double fr = effFormant();
        double thS = std::min (T, (double) thCapSrc);
        double thO = thS / fr;
        if (thO > (double) thCap) { thO = (double) thCap; thS = thO * fr; }
        thO = std::max (4.0, thO);
        const double fc = std::min (1.0, 1.0 / fr);
        const double apos = (double) a.pos;

        const long long p0 = std::max<long long> (curM + 1, (long long) std::ceil (c - thO));
        const long long p1 = (long long) std::floor (c + thO);
        for (long long p = p0; p <= p1; ++p)
        {
            const double dj = (double) p - c;
            const double w = 0.5 + 0.5 * std::cos (kPi * dj / thO);
            const double v = readSinc (apos + dj * fr, fc);
            const size_t oi = (size_t) (p & kOutMask);
            acc[oi]  += w * v;
            wacc[oi] += w;
        }
        sNext += Tsyn;
    }

    double sr;
    int D = 1024, thCap = 400, thCapSrc = 500, Tu = 240, Lc = 160, trackHop = 128, dec = 4;
    double srD = 12000.0;

    std::vector<double> inR, acc, wacc, dR, prevSeg, yinBuf, dif, cmnd, sincT, winT;
    long long n = 0, dn = 0, curM = -1;
    double decAcc = 0.0, decPrev = 0.0;
    int decCnt = 0;

    double sNext = 0.0;
    Mark marks[128];
    int firstId = 0, lastId = 0;
    bool haveSeg = false;

    bool trackVoiced = false;
    double trackT = 0.0, prevTauD = 0.0;
    int voicedCount = 0, unvoicedCount = 0;

    double pitch = 1.0, formant = 1.0, robotHz_ = 0.0, bypassMix = 1.0;
    double pIn = 0.0, pOut = 0.0, lvlGain = 1.0, lvlCoef = 0.001, lvlSmooth = 0.001, lvlSmoothDown = 0.01;
    double intonation_ = 1.0, vibCents = 0.0, vibHz = 5.5, logMean = 0.0, lastVoicedC = 0.0;
    bool haveMean = false;
    bool keep = true;
    double minF0 = 80.0, maxF0 = 625.0;
};

} // namespace pitcher
