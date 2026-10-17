#include "CutFilters.h"

#include "Mapping.h"

namespace ba
{

void CutFilters::prepare (double sampleRate)
{
    fs = sampleRate;
    factor = oversamplingFor (fs);
    rate = fs * factor;
    for (auto* stage : { &up2, &down2 })
        for (auto& h : *stage) h.setup (6, 0.137);

    lowLog.prepare (rate, 0.02);
    highLog.prepare (rate, 0.02);
    lowLog.snap (lowLog.getTarget());
    highLog.snap (highLog.getTarget());
    lowFade.prepare (rate, 0.02);
    highFade.prepare (rate, 0.02);
    pathFade.prepare (fs, 0.02);
    lowFade.settle();
    highFade.settle();
    pathFade.settle();
    lowShown = highShown = -1.0;
    reset();
}

void CutFilters::reset()
{
    for (auto* stage : { &up1, &up2, &down2, &down1 })
        for (auto& h : *stage) h.reset();
    for (auto& ch : lowState)
        for (auto& s : ch) s.reset();
    for (auto& ch : highState)
        for (auto& s : ch) s.reset();
    highCold = true;
}

void CutFilters::setTargets (float lowCut, float highCut) noexcept
{
    const bool lowOn = ! map::lowCutOff (lowCut), highOn = ! map::highCutOff (highCut);
    // A silent filter jumps to the knob, so it wakes up where the knob is.
    const double lowTo = std::log2 (map::lowCutHz (lowCut)), highTo = std::log2 (map::highCutHz (highCut));
    if (lowFade.isOff()) lowLog.snap (lowTo);
    else lowLog.setTarget (lowTo);
    if (highFade.isOff()) highLog.snap (highTo);
    else highLog.setTarget (highTo);
    lowFade.setOn (lowOn);
    highFade.setOn (highOn);
    pathFade.setOn (lowOn || highOn);
}

// Both stages share the cutoff; the same coefficients serve the high- and the lowpass.
void CutFilters::coefficients (double cutoffHz, double sr, dsp::SvfCoeffs& c1, dsp::SvfCoeffs& c2) noexcept
{
    const double g = dsp::SvfCoeffs::prewarp (cutoffHz, sr);
    c1.set (g, kQ1);
    c2.set (g, kQ2);
}

void CutFilters::step (double* v) noexcept
{
    const double lowNow = lowLog.next(), highNow = highLog.next();
    if (lowNow != lowShown)
    {
        coefficients (std::exp2 (lowNow), rate, low1, low2);
        lowShown = lowNow;
    }
    if (highNow != highShown)
    {
        coefficients (std::exp2 (highNow), rate, high1, high2);
        highShown = highNow;
    }

    const double l = lowFade.next(), h = highFade.next();
    if (h > 0.0 && highCold)
    {
        // A lowpass waking from rest would start at zero under a running
        // signal: start it settled on the signal instead.
        for (int c = 0; c < 2; ++c)
            for (auto& s : highState[(size_t) c])
                s = { 0.0, v[c] };
        highCold = false;
    }
    for (int c = 0; c < 2; ++c)
    {
        double x = v[c];
        if (l > 0.0)
        {
            const double y = lowState[(size_t) c][1].highpass (low2, lowState[(size_t) c][0].highpass (low1, x));
            x += l * (y - x);
        }
        if (h > 0.0)
        {
            const double y = highState[(size_t) c][1].lowpass (high2, highState[(size_t) c][0].lowpass (high1, x));
            x += h * (y - x);
        }
        v[c] = x;
    }
}

// One sample through the oversampled path; with `filter` false only the
// halfbands run (to settle them).
void CutFilters::run (const double* in, double* out, bool filter) noexcept
{
    if (factor == 1)
    {
        out[0] = in[0];
        out[1] = in[1];
        if (filter) step (out);
        return;
    }
    double a[2], b[2];
    for (int c = 0; c < 2; ++c)
        up1[(size_t) c].up (in[c], a[c], b[c]);
    if (factor == 2)
    {
        if (filter)
        {
            step (a);
            step (b);
        }
    }
    else
    {
        double a0[2], a1[2], b0[2], b1[2];
        for (int c = 0; c < 2; ++c)
        {
            up2[(size_t) c].up (a[c], a0[c], a1[c]);
            up2[(size_t) c].up (b[c], b0[c], b1[c]);
        }
        if (filter)
        {
            step (a0);
            step (a1);
            step (b0);
            step (b1);
        }
        for (int c = 0; c < 2; ++c)
        {
            a[c] = down2[(size_t) c].down (a0[c], a1[c]);
            b[c] = down2[(size_t) c].down (b0[c], b1[c]);
        }
    }
    for (int c = 0; c < 2; ++c)
        out[c] = down1[(size_t) c].down (a[c], b[c]);
}

void CutFilters::process (float* left, float* right, int n) noexcept
{
    if (isBypassed())
    {
        if (! pathFade.isTargetOn())
        {
            // Resting: only remember the latest input, to wake up settled.
            for (int i = std::max (0, n - kHistory); i < n; ++i)
            {
                history[0][(size_t) historyPos] = left[i];
                history[1][(size_t) historyPos] = right[i];
                historyPos = (historyPos + 1) & (kHistory - 1);
            }
            return;
        }
        // Waking up: run the halfbands over the recent past, so the path
        // fades in from its steady state rather than from a cold transient.
        reset();
        for (int k = 0; k < kHistory; ++k)
        {
            const int at = (historyPos + k) & (kHistory - 1);
            const double in[2] = { history[0][(size_t) at], history[1][(size_t) at] };
            double out[2];
            run (in, out, false);
        }
    }

    for (int i = 0; i < n; ++i)
    {
        const double in[2] = { left[i], right[i] };
        double out[2];
        run (in, out, true);
        // The oversampled path delays by a few samples, so it fades in and
        // out against the untouched signal rather than switching.
        const double p = pathFade.next();
        left[i] = (float) (in[0] + p * (out[0] - in[0]));
        right[i] = (float) (in[1] + p * (out[1] - in[1]));
    }

    // A filter that has faded out starts from rest when it comes back.
    if (lowFade.isOff())
        for (auto& ch : lowState)
            for (auto& s : ch) s.reset();
    if (highFade.isOff())
    {
        for (auto& ch : highState)
            for (auto& s : ch) s.reset();
        highCold = true;
    }
    if (isBypassed())
    {
        reset();
        history = {};
    }
}

double CutFilters::magnitude (double hz, float lowCut, float highCut, double sr) noexcept
{
    const double r = sr * oversamplingFor (sr);
    double m = 1.0;
    if (! map::lowCutOff (lowCut))
    {
        const double fc = map::lowCutHz (lowCut);
        m *= dsp::svfMagnitude (true, fc, kQ1, hz, r) * dsp::svfMagnitude (true, fc, kQ2, hz, r);
    }
    if (! map::highCutOff (highCut))
    {
        const double fc = map::highCutHz (highCut);
        m *= dsp::svfMagnitude (false, fc, kQ1, hz, r) * dsp::svfMagnitude (false, fc, kQ2, hz, r);
    }
    return m;
}

} // namespace ba
