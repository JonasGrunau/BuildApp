#include "Buildup.h"

#include "Mapping.h"

#include <algorithm>

namespace ba
{

void Buildup::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    pitch.prepare (fs);
    filters.prepare (fs);
    noise.prepare (fs);
    reverb.prepare (fs);
    for (auto* s : { &width, &mix, &gain })
    {
        s->prepare (fs, 0.02);
        s->snap (s->getTarget());
    }
    dryL.assign ((size_t) std::max (1, maxBlockSize), 0.f);
    dryR.assign ((size_t) std::max (1, maxBlockSize), 0.f);
}

void Buildup::reset()
{
    pitch.reset();
    filters.reset();
    noise.reset();
    reverb.reset();
}

void Buildup::set (const Settings& s) noexcept
{
    pitch.setSemitones (s.pitch);
    filters.setTargets (s.lowCut, s.highCut);
    noise.setTargets (s.noise, s.noisePitch);
    reverb.setAmount (s.reverb);
    width.setTarget (map::width (s.stereo));
    mix.setTarget (std::clamp ((double) s.mix, 0.0, 1.0));
    gain.setTarget (map::dbToGain (std::clamp ((double) s.levelDb, map::kLevelMin, map::kLevelMax)));
}

void Buildup::process (float* left, float* right, int n) noexcept
{
    const int chunk = (int) dryL.size();
    for (int start = 0; start < n; start += chunk)
        processChunk (left + start, right + start, std::min (chunk, n - start));
}

void Buildup::processChunk (float* left, float* right, int n) noexcept
{
    const bool wet = mix.settled() && mix.get() == 1.0;
    if (! wet)
    {
        std::copy (left, left + n, dryL.data());
        std::copy (right, right + n, dryR.data());
    }

    pitch.process (left, right, n);
    filters.process (left, right, n);
    noise.process (left, right, n);
    reverb.process (left, right, n);

    if (! (width.settled() && width.get() == 1.0))
        for (int i = 0; i < n; ++i)
        {
            const double w = width.next();
            const double m = 0.5 * ((double) left[i] + right[i]), s = 0.5 * ((double) left[i] - right[i]) * w;
            left[i] = (float) (m + s);
            right[i] = (float) (m - s);
        }

    if (! wet)
        for (int i = 0; i < n; ++i)
        {
            const double k = mix.next();
            left[i] = (float) (dryL[(size_t) i] + k * ((double) left[i] - dryL[(size_t) i]));
            right[i] = (float) (dryR[(size_t) i] + k * ((double) right[i] - dryR[(size_t) i]));
        }

    if (! (gain.settled() && gain.get() == 1.0))
        for (int i = 0; i < n; ++i)
        {
            const double g = gain.next();
            left[i] = (float) (left[i] * g);
            right[i] = (float) (right[i] * g);
        }
}

} // namespace ba
