#include "CutFilters.h"

#include "Mapping.h"

namespace ba
{

// ---- One cut ---------------------------------------------------------------------

void CutFilters::Cut::prepare (double rate)
{
    setRate (rate);
    log2Hz.snap (log2Hz.getTarget());
    peak.snap (resonanceAt (log2Hz.get()));
    fade.settle();
    reset();
}

void CutFilters::Cut::setRate (double rate) noexcept
{
    now.setSampleRate (rate);
    before.setSampleRate (rate);
    log2Hz.prepare (rate, 0.02);
    peak.prepare (rate, 0.03);
    fade.prepare (rate, 0.02);
    change.prepare (rate, 0.03);
}

void CutFilters::Cut::reset() noexcept
{
    now.reset();
    before.reset();
    change.snap (true);
    cold = true;
}

double CutFilters::Cut::resonanceAt (double hz) noexcept
{
    if (! autoReso) return manual;
    if (hz != autoAt)
    {
        autoValue = ba::autoReso (lowpass, character (lowpass, type), std::exp2 (hz));
        autoAt = hz;
    }
    return autoValue;
}

void CutFilters::Cut::setTarget (bool on, double hz, const CutKnobs& k) noexcept
{
    const int t = std::clamp (k.type, 0, (int) characters (lowpass).size() - 1);
    manual = std::clamp ((double) k.reso, 0.0, 1.0);
    autoReso = k.autoReso;
    // A silent filter jumps to the knobs and the type, so it wakes up where they are.
    if (fade.isOff())
    {
        log2Hz.snap (hz);
        if (t != type) now.setCharacter (character (lowpass, t));
        type = t;
        autoAt = -1.0;
        peak.snap (resonanceAt (hz));
        change.snap (true);
    }
    else
    {
        log2Hz.setTarget (hz);
        if (t != type)
        {
            before = now;
            now.setCharacter (character (lowpass, t));
            type = t;
            autoAt = -1.0;
            change.snap (false);
            change.setOn (true);
        }
    }
    fade.setOn (on);
}

void CutFilters::Cut::process (double* v) noexcept
{
    if (fade.isOff()) return;
    const double w = fade.next();
    if (fade.isOff())
    {
        // Faded out: it starts from rest when it comes back.
        reset();
        return;
    }

    const double hz = log2Hz.next();
    peak.setTarget (resonanceAt (hz));
    const double pk = peak.next();
    now.setCutoff (hz, pk);
    if (cold)
    {
        now.prime (v);
        cold = false;
    }
    double y[2] = { v[0], v[1] };
    now.process (y);
    if (! change.isOn())
    {
        const double k = change.next();
        before.setCutoff (hz, pk);
        double z[2] = { v[0], v[1] };
        before.process (z);
        for (int c = 0; c < 2; ++c)
            y[c] = z[c] + k * (y[c] - z[c]);
    }
    for (int c = 0; c < 2; ++c)
        v[c] += w * (y[c] - v[c]);
}

// ---- Both, and the path -------------------------------------------------------------

void CutFilters::prepare (double sampleRate)
{
    fs = sampleRate;
    factor = wantHigh ? oversamplingFor (fs) : 1;
    for (auto* stage : { &up2, &down2 })
        for (auto& h : *stage) h.setup (6, 0.137);
    low.prepare (fs * factor);
    high.prepare (fs * factor);
    pathFade.prepare (fs, 0.02);
    pathFade.settle();
    history = {};
    resetPath();
}

void CutFilters::reset()
{
    low.reset();
    high.reset();
    resetPath();
}

void CutFilters::resetPath() noexcept
{
    for (auto* stage : { &up1, &up2, &down2, &down1 })
        for (auto& h : *stage) h.reset();
}

void CutFilters::setFactor (int f) noexcept
{
    factor = f;
    low.setRate (fs * f);
    high.setRate (fs * f);
}

void CutFilters::setTargets (const CutKnobs& l, const CutKnobs& h) noexcept
{
    const bool lowOn = ! map::lowCutOff (l.position), highOn = ! map::highCutOff (h.position);
    low.setTarget (lowOn, std::log2 (map::lowCutHz (l.position)), l);
    high.setTarget (highOn, std::log2 (map::highCutHz (h.position)), h);
    pathFade.setOn (lowOn || highOn);
}

// One host sample through the path; with `filter` false only the halfbands
// run (to settle them).
void CutFilters::run (const double* in, double* out, bool filter) noexcept
{
    const auto cuts = [&] (double* v) {
        if (! filter) return;
        low.process (v);
        high.process (v);
    };
    if (factor == 1)
    {
        out[0] = in[0];
        out[1] = in[1];
        cuts (out);
        return;
    }
    double a[2], b[2];
    for (int c = 0; c < 2; ++c)
        up1[(size_t) c].up (in[c], a[c], b[c]);
    if (factor == 2)
    {
        cuts (a);
        cuts (b);
    }
    else
    {
        double a0[2], a1[2], b0[2], b1[2];
        for (int c = 0; c < 2; ++c)
        {
            up2[(size_t) c].up (a[c], a0[c], a1[c]);
            up2[(size_t) c].up (b[c], b0[c], b1[c]);
        }
        cuts (a0);
        cuts (a1);
        cuts (b0);
        cuts (b1);
        for (int c = 0; c < 2; ++c)
        {
            a[c] = down2[(size_t) c].down (a0[c], a1[c]);
            b[c] = down2[(size_t) c].down (b0[c], b1[c]);
        }
    }
    for (int c = 0; c < 2; ++c)
        out[c] = down1[(size_t) c].down (a[c], b[c]);
}

void CutFilters::settlePath() noexcept
{
    resetPath();
    if (factor == 1) return;
    for (int k = 0; k < kHistory; ++k)
    {
        const int at = (historyPos + k) & (kHistory - 1);
        const double in[2] = { history[0][(size_t) at], history[1][(size_t) at] };
        double out[2];
        run (in, out, false);
    }
}

void CutFilters::process (float* left, float* right, int n) noexcept
{
    if (const int want = wantHigh ? oversamplingFor (fs) : 1; want != factor)
    {
        setFactor (want);
        if (! isBypassed()) settlePath();
    }

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
        // Waking up: the path fades in from its steady state, not from a cold transient.
        settlePath();
    }

    for (int i = 0; i < n; ++i)
    {
        history[0][(size_t) historyPos] = left[i];
        history[1][(size_t) historyPos] = right[i];
        historyPos = (historyPos + 1) & (kHistory - 1);

        const double in[2] = { left[i], right[i] };
        double out[2];
        run (in, out, true);
        const double p = pathFade.next();
        left[i] = (float) (in[0] + p * (out[0] - in[0]));
        right[i] = (float) (in[1] + p * (out[1] - in[1]));
    }

    if (isBypassed())
        resetPath();
}

double CutFilters::resonance (bool lowpass, const CutKnobs& k) noexcept
{
    if (! k.autoReso) return std::clamp ((double) k.reso, 0.0, 1.0);
    return autoReso (lowpass, character (lowpass, k.type), lowpass ? map::highCutHz (k.position) : map::lowCutHz (k.position));
}

double CutFilters::magnitude (double hz, const CutKnobs& l, const CutKnobs& h, double fs) noexcept
{
    double m = 1.0;
    if (! map::lowCutOff (l.position))
        m *= CharacterFilter::magnitude (false, character (false, l.type), map::lowCutHz (l.position), resonance (false, l), hz, fs);
    if (! map::highCutOff (h.position))
        m *= CharacterFilter::magnitude (true, character (true, h.type), map::highCutHz (h.position), resonance (true, h), hz, fs);
    return m;
}

} // namespace ba
