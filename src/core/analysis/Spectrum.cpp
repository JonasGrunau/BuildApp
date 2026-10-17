#include "Spectrum.h"

#include <algorithm>
#include <cmath>

namespace ba
{

double Spectrum::pointHz (int i) noexcept
{
    return kLowHz * std::pow (kHighHz / kLowHz, (double) i / (kPoints - 1));
}

void Spectrum::prepare (double sampleRate)
{
    fs = sampleRate;
    int order = 12;
    while ((1 << order) < 4096.0 * fs / 48000.0 && order < 14) ++order;   // 16384 at most: half a tap
    size = 1 << order;
    fft = std::make_unique<dsp::Fft> (order);
    window = dsp::hann (size);
    double sum = 0.0;
    for (float w : window) sum += w;
    scale = 2.0 / sum;   // a full-scale sine reads 0 dB
    a.assign ((size_t) size, 0.f);
    b.assign ((size_t) size, 0.f);
    fa.assign ((size_t) size / 2 + 1, {});
    fb.assign ((size_t) size / 2 + 1, {});
    clear();
}

void Spectrum::clear()
{
    inDb.assign (kPoints, (float) kFloorDb);
    outDb.assign (kPoints, (float) kFloorDb);
}

bool Spectrum::fall (float db)
{
    bool any = false;
    for (auto* v : { &inDb, &outDb })
        for (auto& x : *v)
        {
            x = std::max ((float) kFloorDb, x - db);
            any = any || x > (float) kFloorDb;
        }
    return any;
}

bool Spectrum::update (const Tap& input, const Tap& output)
{
    if (fft == nullptr || ! input.latest (a.data(), size) || ! output.latest (b.data(), size))
        return false;
    fft->forwardRealPair (a.data(), b.data(), size, window.data(), fa.data(), fb.data(), scratch);
    readOut (fa.data(), inDb);
    readOut (fb.data(), outDb);
    return true;
}

void Spectrum::readOut (const dsp::cplx* bins, std::vector<float>& shown) const
{
    const double binHz = fs / size;
    const int last = size / 2;
    const double halfStep = std::pow (kHighHz / kLowHz, 0.5 / (kPoints - 1));
    const auto power = [&] (int k) { return std::norm (bins[std::clamp (k, 0, last)]) * scale * scale; };

    // Each point averages the power over 1/12 octave (or interpolates
    // between bins where they are sparser than that): calm, readable curves.
    const double halfBand = std::max (halfStep, std::pow (2.0, 1.0 / 24.0));
    for (int i = 0; i < kPoints; ++i)
    {
        const double f = pointHz (i);
        const double lo = f / halfBand / binHz, hi = f * halfBand / binHz;
        double p;
        if (hi - lo < 1.0)
        {
            const double x = f / binHz;
            const int k = (int) x;
            p = power (k) + (x - k) * (power (k + 1) - power (k));
        }
        else
        {
            const int k0 = (int) std::ceil (lo), k1 = std::min (last, (int) std::floor (hi));
            p = 0.0;
            for (int k = k0; k <= k1; ++k) p += power (k);
            p /= std::max (1, k1 - k0 + 1);
        }
        const double db = std::max (kFloorDb, 10.0 * std::log10 (p + 1e-24) + 4.5 * std::log2 (f / 1000.0));
        auto& s = shown[(size_t) i];
        s += (float) ((db > s ? 0.5 : 0.12) * (db - s));
    }
}

} // namespace ba
