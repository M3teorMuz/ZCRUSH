// Консольная обёртка над PitcherDSP.h / VoiceFX.h для автоматических проверок.
//   pitcher_cli voice   in.f32 out.f32 sr voiceIdx userPitchSt userFormantSt preserve fft [amount]   (fft=0 -> Live)
//   pitcher_cli process in.f32 out.f32 sr pitchSemi formantSemi preserve fft [maxF0] [raw]
//   pitcher_cli stress  sr fft seconds
//   pitcher_cli bench   sr fft seconds
#include "../Source/PitcherDSP.h"
#include "../Source/VoiceFX.h"
#include "../Source/LiveShifter.h"
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <random>
#include <string>

using namespace pitcher;

// fft == 0 -> движок Live (PSOLA), иначе Smooth (STFT) с таким размером FFT
static std::unique_ptr<IShifter> makeEngine (int fft, double sr)
{
    if (fft == 0) return std::make_unique<LiveShifter> (sr);
    return std::make_unique<ChannelShifter> (fft, sr);
}

static std::vector<float> readF32 (const char* path)
{
    std::vector<float> v;
    FILE* f = std::fopen (path, "rb");
    if (! f) return v;
    std::fseek (f, 0, SEEK_END);
    long n = std::ftell (f);
    std::fseek (f, 0, SEEK_SET);
    v.resize ((size_t) n / sizeof (float));
    if (std::fread (v.data(), sizeof (float), v.size(), f) != v.size()) v.clear();
    std::fclose (f);
    return v;
}

int main (int argc, char** argv)
{
    if (argc < 2) return 1;
    const std::string mode = argv[1];

    if (mode == "voice" && argc >= 10)
    {
        auto in = readF32 (argv[2]);
        const double sr = std::atof (argv[4]);
        const int voice = std::atoi (argv[5]);
        const double up = std::atof (argv[6]);
        const double uf = std::atof (argv[7]);
        const bool keep = std::atoi (argv[8]) != 0;
        const int n = std::atoi (argv[9]);
        const double amount = argc >= 11 ? std::atof (argv[10]) : 1.0;

        auto eng = makeEngine (n, sr);
        IShifter& s = *eng;
        const int lat = s.latencySamples();
        VoiceColor color (sr, 0);
        color.setVoice (voice, amount);
        const ResolvedShift r = resolveVoice (voice, amount, up, uf);
        s.setParameters (r.pitchRatio, r.formantRatio, keep, 500.0);
        s.setRobot (r.robotHz);
        s.setCharacter (r.intonation, r.vibratoCents, r.vibratoHz);

        const size_t len = in.size();
        std::vector<float> out;
        out.reserve (len);
        for (size_t i = 0; i < len + (size_t) lat; ++i)
        {
            float y = color.process (s.processSample (i < len ? in[i] : 0.0f));
            if (i >= (size_t) lat) out.push_back (y);
        }
        FILE* f = std::fopen (argv[3], "wb");
        std::fwrite (out.data(), sizeof (float), out.size(), f);
        std::fclose (f);
        return 0;
    }

    if (mode == "stressvoices" && argc >= 5)
    {
        const double sr = std::atof (argv[2]);
        const int n = std::atoi (argv[3]);
        const double secs = std::atof (argv[4]);
        std::mt19937 rng (99);
        std::uniform_real_distribution<double> u (-1.0, 1.0);
        double peak = 0.0;
        long bad = 0, total = 0;
        for (int v = 0; v < kNumVoices; ++v)
        {
            auto eng = makeEngine (n, sr);
            IShifter& s = *eng;
            VoiceColor color (sr, v & 1);
            color.setVoice (v, 1.0);
            const long len = (long) (sr * secs);
            for (long i = 0; i < len; ++i)
            {
                if (i % 512 == 0)
                {
                    const ResolvedShift r = resolveVoice (v, 0.5 + 0.5 * u (rng), u (rng) * 12.0, u (rng) * 12.0);
                    s.setParameters (r.pitchRatio, r.formantRatio, (rng() & 1) != 0, 500.0);
                    s.setRobot (r.robotHz);
                    s.setCharacter (r.intonation, r.vibratoCents, r.vibratoHz);
                }
                float x = (i / 24000) % 2 ? (float) (0.5 * u (rng)) + (float) (0.4 * std::sin (kTwoPi * 180.0 * i / sr)) : 0.0f;
                const float y = color.process (s.processSample (x));
                ++total;
                if (! std::isfinite (y)) ++bad; else peak = std::max (peak, (double) std::fabs (y));
            }
        }
        std::printf ("stressvoices peak=%.3f nonfinite=%ld total=%ld\n", peak, bad, total);
        return (bad == 0 && peak < 8.0) ? 0 : 2;
    }

    if (mode == "process" && argc >= 9)
    {
        auto in = readF32 (argv[2]);
        const double sr = std::atof (argv[4]);
        const double ps = std::atof (argv[5]);
        const double fs = std::atof (argv[6]);
        const bool keep = std::atoi (argv[7]) != 0;
        const int n = std::atoi (argv[8]);
        const double maxF0 = argc >= 10 ? std::atof (argv[9]) : 500.0;
        const bool raw = argc >= 11 && std::atoi (argv[10]) != 0;

        auto eng = makeEngine (n, sr);
        IShifter& s = *eng;
        const int lat = s.latencySamples();
        s.setParameters (std::pow (2.0, ps / 12.0), std::pow (2.0, fs / 12.0), keep, maxF0);

        const size_t len = in.size();
        std::vector<float> out;
        out.reserve (len);
        for (size_t i = 0; i < len + (size_t) lat; ++i)
        {
            float y = s.processSample (i < len ? in[i] : 0.0f);
            if (raw || i >= (size_t) lat) out.push_back (y);
        }

        FILE* f = std::fopen (argv[3], "wb");
        std::fwrite (out.data(), sizeof (float), out.size(), f);
        std::fclose (f);
        return 0;
    }

    if (mode == "stress" && argc >= 5)
    {
        const double sr = std::atof (argv[2]);
        const int n = std::atoi (argv[3]);
        const double secs = std::atof (argv[4]);
        std::mt19937 rng (1234);
        std::uniform_real_distribution<double> u (-1.0, 1.0);
        auto eng = makeEngine (n, sr);
        IShifter& s = *eng;

        double peak = 0.0;
        long bad = 0, total = 0;
        double scenPeak[6] = {0, 0, 0, 0, 0, 0};
        const long len = (long) (sr * secs);
        for (int scenario = 0; scenario < 6; ++scenario)
        {
            for (long i = 0; i < len; ++i)
            {
                if (i % 512 == 0)   // параметры меняются каждый блок, как автоматизация
                    s.setParameters (std::pow (2.0, u (rng) * 24.0 / 12.0),
                                     std::pow (2.0, u (rng) * 12.0 / 12.0),
                                     (rng() & 1) != 0, 300.0 + 400.0 * (u (rng) + 1.0));
                float x = 0.0f;
                switch (scenario)
                {
                    case 0: x = 0.0f; break;                                    // тишина
                    case 1: x = (float) (0.5 * u (rng)); break;                 // шум
                    case 2: x = 0.8f; break;                                    // DC
                    case 3: x = (i % 4800 == 0) ? 1.0f : 0.0f; break;           // импульсы
                    case 4: x = (float) (0.9 * std::sin (kTwoPi * 440.0 * i / sr)); break;
                    case 5: x = (i / 20000) % 2 ? (float) (0.7 * u (rng)) : 0.0f; break; // шум/тишина
                }
                const float y = s.processSample (x);
                ++total;
                if (! std::isfinite (y)) ++bad;
                else { peak = std::max (peak, (double) std::fabs (y));
                       scenPeak[scenario] = std::max (scenPeak[scenario], (double) std::fabs (y)); }
            }
        }
        std::printf ("stress peak=%.3f nonfinite=%ld total=%ld [silence %.2f noise %.2f dc %.2f impulses %.2f sine %.2f burst %.2f]\n",
                     peak, bad, total, scenPeak[0], scenPeak[1], scenPeak[2], scenPeak[3], scenPeak[4], scenPeak[5]);
        return (bad == 0 && peak < 8.0) ? 0 : 2;
    }

    if (mode == "bench" && argc >= 5)
    {
        const double sr = std::atof (argv[2]);
        const int n = std::atoi (argv[3]);
        const double secs = std::atof (argv[4]);
        auto eng = makeEngine (n, sr);
        IShifter& s = *eng;
        s.setParameters (std::pow (2.0, 5.0 / 12.0), 1.0, true, 500.0);
        std::mt19937 rng (1);
        std::uniform_real_distribution<float> u (-0.3f, 0.3f);
        const long len = (long) (sr * secs);
        auto t0 = std::chrono::steady_clock::now();
        float acc = 0;
        for (long i = 0; i < len; ++i)
            acc += s.processSample (u (rng) + 0.5f * (float) std::sin (kTwoPi * 150.0 * i / sr));
        auto t1 = std::chrono::steady_clock::now();
        const double el = std::chrono::duration<double> (t1 - t0).count();
        std::printf ("bench N=%d: %.3f s CPU for %.1f s audio (1 channel) => %.1f %% of one core (acc=%f)\n",
                     n, el, secs, 100.0 * el / secs, acc);
        return 0;
    }
    return 1;
}
