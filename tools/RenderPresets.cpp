// Renders every preset's buildup through the chain, to tune the presets by
// measurement and to listen to them.
//   buildapp_render <out-dir> [preset number ...]
// Each preset plays the built-in 145 BPM loop with BUILD rising linearly
// from 0 to 1 over 16 bars, then 4 bars of the drop (BUILD back at 0), into
// <out-dir>/NN-name.wav (00-dry.wav is the loop untouched). Per bar it prints
// loudness re the dry loop (LU, K-weighted), brightness re dry (octaves),
// width (side/mid, dB) and the peak (dBFS); then, for each preset, the one
// that sounds most alike and how far apart they are (octave-band levels
// over the 16 bars, RMS dB).

#include "TestUtil.h"
#include "dsp/Fft.h"
#include "fx/Presets.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace testutil;

namespace
{
    constexpr double kFs = 48000.0, kBar = 60.0 / 145.0 * 4.0;
    constexpr int kBuildBars = 16, kDropBars = 4, kBands = 9;   // octaves from 31 Hz to 8 kHz

    // BUILD at time t: a linear rise over the build, then 0.
    float buildAt (double t) { return t < kBuildBars * kBar ? (float) (t / (kBuildBars * kBar)) : 0.f; }

    Stereo render (const Stereo& loop, const ba::Preset* p)
    {
        ba::Buildup chain;
        ba::Settings own;
        chain.set (p != nullptr ? ba::applyPreset (*p, 0.f, own) : own);
        constexpr int block = 480;
        chain.prepare (kFs, block);
        Stereo y = loop;
        for (size_t start = 0; start < y.size(); start += block)
        {
            const int n = (int) std::min<size_t> (block, y.size() - start);
            if (p != nullptr) chain.set (ba::applyPreset (*p, buildAt ((double) start / kFs), own));
            chain.process (y.left.data() + start, y.right.data() + start, n);
        }
        return y;
    }

    // Octave-band levels of the mid signal per bar, dB.
    std::vector<std::array<double, kBands>> bands (const Stereo& s, int bars)
    {
        const ba::dsp::Fft fft (12);
        const int n = fft.size();
        const auto window = ba::dsp::hann (n);
        std::vector<ba::dsp::cplx> a ((size_t) n / 2 + 1), b ((size_t) n / 2 + 1), scratch;
        std::vector<float> mid ((size_t) n);
        std::vector<std::array<double, kBands>> out ((size_t) bars);
        const auto barLen = (size_t) (kBar * kFs);
        for (int bar = 0; bar < bars; ++bar)
        {
            std::array<double, kBands> e {};
            for (size_t at = (size_t) bar * barLen; at + (size_t) n <= (size_t) (bar + 1) * barLen; at += (size_t) n / 2)
            {
                for (int i = 0; i < n; ++i) mid[(size_t) i] = 0.5f * (s.left[at + (size_t) i] + s.right[at + (size_t) i]);
                fft.forwardRealPair (mid.data(), mid.data(), n, window.data(), a.data(), b.data(), scratch);
                for (int k = 1; k <= n / 2; ++k)
                {
                    const double hz = k * kFs / n;
                    const int band = (int) std::floor (std::log2 (hz / 31.25 * std::sqrt (2.0)));
                    if (band >= 0 && band < kBands) e[(size_t) band] += std::norm (a[(size_t) k]);
                }
            }
            for (auto& v : e) v = 10.0 * std::log10 (v + 1e-12);
            out[(size_t) bar] = e;
        }
        return out;
    }

    double peakDb (const Stereo& s, size_t from, size_t to)
    {
        double p = 0.0;
        for (size_t i = from; i < to; ++i) p = std::max ({ p, (double) std::abs (s.left[i]), (double) std::abs (s.right[i]) });
        return db (p);
    }
} // namespace

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::vector<int> pick;
    for (int i = 2; i < argc; ++i)
    {
        const int n = std::atoi (argv[i]);
        if (n < 1 || n > (int) std::size (ba::kPresets))
        {
            std::fprintf (stderr, "No preset %s (1 to %d)\n", argv[i], (int) std::size (ba::kPresets));
            return 1;
        }
        pick.push_back (n - 1);
    }
    if (pick.empty())
        for (int i = 0; i < (int) std::size (ba::kPresets); ++i) pick.push_back (i);

    std::error_code made;
    std::filesystem::create_directories (dir, made);
    const auto save = [] (const std::string& path, const Stereo& s) {
        if (! writeWav (path.c_str(), s, kFs)) std::fprintf (stderr, "Could not write %s\n", path.c_str());
    };

    const int bars = kBuildBars + kDropBars;
    const auto barLen = (size_t) (kBar * kFs);
    const auto loop = psyLoop (kFs, bars * kBar);
    save (dir + "/00-dry.wav", loop);
    std::vector<double> dryLu, dryHz;
    for (int bar = 0; bar < bars; ++bar)
    {
        dryLu.push_back (loudness (loop, kFs, (size_t) bar * barLen, (size_t) (bar + 1) * barLen));
        dryHz.push_back (brightness (loop, kFs, (size_t) bar * barLen, (size_t) (bar + 1) * barLen));
    }
    const auto dryBands = bands (loop, kBuildBars);

    std::printf ("bar      ");
    for (int bar = 1; bar <= bars; ++bar) std::printf (bar <= kBuildBars ? "%5d" : "   d%d", bar <= kBuildBars ? bar : bar - kBuildBars);
    std::printf ("\n");

    std::vector<std::vector<std::array<double, kBands>>> shapes;
    for (const int i : pick)
    {
        const auto& p = ba::kPresets[(size_t) i];
        const auto y = render (loop, &p);
        char name[256];
        std::snprintf (name, sizeof (name), "%s/%02d-%s.wav", dir.c_str(), i + 1, p.name);
        save (name, y);

        std::printf ("\n%02d %s\n", i + 1, p.name);
        const auto row = [&] (const char* label, auto value) {
            std::printf ("%-9s", label);
            for (int bar = 0; bar < bars; ++bar) std::printf ("%5.1f", value ((size_t) bar * barLen, (size_t) (bar + 1) * barLen, bar));
            std::printf ("\n");
        };
        row ("LU re dry", [&] (size_t a, size_t b, int bar) { return loudness (y, kFs, a, b) - dryLu[(size_t) bar]; });
        row ("bright oct", [&] (size_t a, size_t b, int bar) { return std::log2 (brightness (y, kFs, a, b) / dryHz[(size_t) bar]); });
        row ("width dB", [&] (size_t a, size_t b, int) { return widthDb (y, a, b); });
        row ("peak dBFS", [&] (size_t a, size_t b, int) { return peakDb (y, a, b); });

        auto shape = bands (y, kBuildBars);
        for (size_t bar = 0; bar < shape.size(); ++bar)
            for (size_t k = 0; k < kBands; ++k) shape[bar][k] -= dryBands[bar][k];
        shapes.push_back (shape);
    }

    std::printf ("\nnearest preset (octave-band levels over the build, RMS dB apart)\n");
    for (size_t i = 0; i < shapes.size(); ++i)
    {
        double best = 1e9;
        size_t nearest = i;
        for (size_t j = 0; j < shapes.size(); ++j)
        {
            if (j == i) continue;
            double e = 0.0;
            for (size_t bar = 0; bar < shapes[i].size(); ++bar)
                for (size_t k = 0; k < kBands; ++k) e += std::pow (std::clamp (shapes[i][bar][k], -40.0, 20.0) - std::clamp (shapes[j][bar][k], -40.0, 20.0), 2.0);
            const double d = std::sqrt (e / (double) (shapes[i].size() * kBands));
            if (d < best) { best = d; nearest = j; }
        }
        std::printf ("  %-14s %-14s %5.1f dB\n", ba::kPresets[(size_t) pick[i]].name, ba::kPresets[(size_t) pick[nearest]].name, best);
    }
    return 0;
}
