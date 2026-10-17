#pragma once

#include "FilterStyles.h"
#include "Mapping.h"

#include <span>

// What the low cut and high cut type menus offer: a filter style with a
// slope and a drive, picked from FabFilter Volcano 3's factory presets for
// buildups and breakdowns (the preset each one comes from is noted), and how
// its resonance (Volcano's peak) moves with the cutoff in auto mode. Each
// cut has its own list; index 0 is the default.
namespace ba
{
    struct Character
    {
        const char* name;   // menu text
        const char* help;   // one line for the editor's footer
        int style;          // style::Style
        int slope;          // dB/oct: 12, 24 or 48
        double driveDb;     // Volcano's drive, -18..+18 dB (Clean ignores it)
        bool build;         // a buildup sound (tension) rather than a breakdown one
        double autoFrom;    // auto resonance with the cut barely in ...
        double autoTo;      // ... and where it bites (see autoReso)
    };

    // Low cut: highpass.
    inline constexpr Character kLowCutCharacters[] = {
        { "Classic 24 / Rise", "FabFilter One's filter at 24 dB/oct, drive backed off: the buildup riser, singing more as it climbs (Arpeggio 1, Drum Blender).", style::classic, 24, -18.0, true, 0.15, 0.7 },
        { "Classic 48 / Scream", "Classic at 48 dB/oct: steep, and screaming by the top of the climb (Inside My Mind).", style::classic, 48, 0.0, true, 0.25, 0.8 },
        { "Raw 12 / Bang", "Raw, 12 dB/oct, driven resonance: gritty and loud (Bangin').", style::raw, 12, -18.0, true, 0.12, 0.5 },
        { "Smooth 12 / Pump", "Smooth, 12 dB/oct, a little drive: round and punchy (Beat Filter 2).", style::smooth, 12, 3.8, true, 0.15, 0.48 },
        { "Gentle 12 / Soft", "Gentle, 12 dB/oct, Volcano's default style: the softest climb, a rounded corner with a mild lift.", style::gentle, 12, 0.0, true, 0.12, 0.45 },
        { "Extreme 48 / Feed", "Extreme at 48 dB/oct: wild and steep (Power Feed).", style::extreme, 48, 0.0, true, 0.1, 0.42 },
        { "Hard 48 / Flay", "Hard, 48 dB/oct: a steep wall with a clean whistle (Side Flay).", style::hard, 48, 0.0, true, 0.08, 0.6 },
        { "Classic 12 / Thin", "Classic, 12 dB/oct, no drive, hardly any resonance: thins the low end out gently (Extreme Filter).", style::classic, 12, -18.0, false, 0.0, 0.15 },
        { "Hollow 48 / Carve", "Hollow at 48 dB/oct, barely resonant: carves the bass out, no ringing (Arpeggio 2).", style::hollow, 48, -18.0, false, 0.0, 0.12 },
        { "Hard 12 / Radio", "Hard, 12 dB/oct, low drive: small-speaker breakdown (Oldie Broken Radio).", style::hard, 12, -11.1, false, 0.05, 0.22 },
        { "Clean 48 / Steep", "Clean, 48 dB/oct: linear, no drive, no clipping (Momentary Glitchy Filter).", style::clean, 48, 0.0, false, 0.05, 0.3 },
    };

    // High cut: lowpass.
    inline constexpr Character kHighCutCharacters[] = {
        { "Gentle 12 / Default", "Volcano's default: Gentle, 12 dB/oct. Smooth general-purpose close.", style::gentle, 12, 0.0, false, 0.2, 0.45 },
        { "Classic 12 / Vintage", "FabFilter One's filter at 12 dB/oct (Vintage).", style::classic, 12, 0.0, false, 0.2, 0.5 },
        { "Smooth 24 / Stutter", "Smooth, 24 dB/oct: a round, dark close (Energetic Stutter).", style::smooth, 24, 0.0, false, 0.08, 0.32 },
        { "Hollow 12 / Warble", "Hollow, 12 dB/oct: a low, rubbery breakdown (Warble).", style::hollow, 12, -18.0, false, 0.15, 0.36 },
        { "Clean 12 / Pure", "Clean, 12 dB/oct: linear, no drive, no clipping (Drum Blender).", style::clean, 12, 0.0, false, 0.2, 0.45 },
        { "Classic 48 / Gate", "Classic at 48 dB/oct: the steep trance-gate sweep (MS Trance Gate 1).", style::classic, 48, -18.0, true, 0.2, 0.68 },
        { "Hard 24 / Montoya", "Hard, 24 dB/oct: bright whistle on the way up (Montoya).", style::hard, 24, 0.0, true, 0.1, 0.5 },
        { "Hard 48 / Drive", "Hard at 48 dB/oct, driven: an aggressive opening (MS Trance Gate 2).", style::hard, 48, 3.7, true, 0.2, 0.6 },
        { "Tube 12 / Funk", "Tube, 12 dB/oct, heavy drive: warm overdrive on the sweep (Funky Phaser).", style::tube, 12, 10.3, true, 0.12, 0.5 },
        { "Extreme 24 / Rampage", "Extreme, 24 dB/oct, hot drive: screaming opener (Screaming Rampage).", style::extreme, 24, 13.1, true, 0.08, 0.3 },
    };

    inline std::span<const Character> characters (bool lowpass) noexcept
    {
        if (lowpass) return kHighCutCharacters;
        return kLowCutCharacters;
    }

    inline const Character& character (bool lowpass, int index) noexcept
    {
        const auto list = characters (lowpass);
        return list[(size_t) std::clamp (index, 0, (int) list.size() - 1)];
    }

    // Auto resonance at a cutoff, from `autoFrom` to `autoTo`. It follows how
    // far the cut is in (t, 0..1 along the knob's travel):
    //   low cut    rises from t 0.1 to t 0.85, so a riser sings more the
    //              higher it climbs and stays dry on the sub-bass;
    //   high cut   rises from fully open (t 0.05) to a peak around 1.4 kHz
    //              (t 0.5), then eases back towards the bottom so a closed
    //              lowpass doesn't boom.
    inline double autoReso (bool lowpass, const Character& c, double cutoffHz) noexcept
    {
        const auto smooth = [] (double x) { x = std::clamp (x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); };
        double w;
        if (lowpass)
        {
            const double t = 1.0 - (double) map::highCutNorm (cutoffHz);
            w = t < 0.5 ? smooth ((t - 0.05) / 0.45) : 1.0 - 0.4 * smooth ((t - 0.5) / 0.5);
        }
        else
        {
            w = smooth (((double) map::lowCutNorm (cutoffHz) - 0.1) / 0.75);
        }
        return c.autoFrom + (c.autoTo - c.autoFrom) * w;
    }
} // namespace ba
