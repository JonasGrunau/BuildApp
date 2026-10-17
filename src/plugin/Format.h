#pragma once

#include <juce_core/juce_core.h>

// Number formatting shared by the parameter texts and the editor.
namespace fmt
{
    inline const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));

    // Fixed decimals; 0 really means none (juce::String (x, 0) prints all of them).
    inline juce::String fixed (double v, int decimals)
    {
        return decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
    }

    // Proper minus sign and fixed decimals: "−3.4", "+2.0".
    inline juce::String signedNumber (double v, int decimals)
    {
        const auto body = fixed (std::abs (v), decimals);
        if (body.containsOnly ("0.")) return body;   // no "−0.0"
        return (v < 0.0 ? minus : juce::String ("+")) + body;
    }

    // "45.2 Hz", "340 Hz", "4.25 kHz", "12.3 kHz".
    inline juce::String hertz (double hz)
    {
        if (hz < 99.95) return fixed (hz, 1) + " Hz";
        if (hz < 999.5) return fixed (hz, 0) + " Hz";
        if (hz < 9995.0) return fixed (hz / 1000.0, 2) + " kHz";
        return fixed (hz / 1000.0, 1) + " kHz";
    }

    // Reads what someone typed: "340", "4.2k", "4.2 kHz", "-3 dB", "−3", "off".
    inline double parse (const juce::String& typed)
    {
        auto t = typed.trim().replace (minus, "-").toLowerCase();
        double v = t.getDoubleValue();
        if (t.containsChar ('k')) v *= 1000.0;
        return v;
    }
}
