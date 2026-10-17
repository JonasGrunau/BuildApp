#pragma once

#include <algorithm>
#include <cmath>

namespace ba::dsp
{

// One-pole glide towards a target, per sample. Lands exactly on the target
// once it is close, so a settled value can be compared with ==.
class Smoother
{
public:
    void prepare (double fs, double timeConstantSeconds) noexcept
    {
        coef = std::exp (-1.0 / (timeConstantSeconds * fs));
    }

    void setTarget (double t) noexcept { target = t; }
    void snap (double t) noexcept { target = value = t; }

    double next() noexcept
    {
        if (value != target)
        {
            value = target + (value - target) * coef;
            if (std::abs (value - target) < epsilon) value = target;
        }
        return value;
    }

    double get() const noexcept { return value; }
    double getTarget() const noexcept { return target; }
    bool settled() const noexcept { return value == target; }

    double epsilon = 1e-6;

private:
    double value = 0.0, target = 0.0, coef = 0.0;
};

// 0..1 fade for switching a stage in and out without a click. It runs
// linearly but comes out as a smoothstep: no kink at either end, so even a
// crossfade between two slightly different signals leaves no trace.
class Fade
{
public:
    void prepare (double fs, double seconds) noexcept { step = 1.0 / std::max (1.0, seconds * fs); }
    void setOn (bool on) noexcept { target = on ? 1.0 : 0.0; }
    void snap (bool on) noexcept { value = target = on ? 1.0 : 0.0; }
    void settle() noexcept { value = target; }

    double next() noexcept
    {
        if (value < target) value = std::min (target, value + step);
        else if (value > target) value = std::max (target, value - step);
        return value * value * (3.0 - 2.0 * value);
    }

    double get() const noexcept { return value; }
    bool isOff() const noexcept { return value == 0.0 && target == 0.0; }
    bool isOn() const noexcept { return value == 1.0 && target == 1.0; }
    bool isTargetOn() const noexcept { return target == 1.0; }

private:
    double value = 0.0, target = 0.0, step = 0.001;
};

} // namespace ba::dsp
