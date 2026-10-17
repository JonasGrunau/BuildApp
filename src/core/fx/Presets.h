#pragma once

#include "Buildup.h"
#include "Characters.h"

#include <array>
#include <cmath>
#include <initializer_list>
#include <span>
#include <string_view>

// Presets: one BUILD knob drives a buildup along a designed path. Each preset
// gives some controls a lane, the control's position along BUILD's travel;
// the controls it gives no lane stay the user's. Output level and dry/wet
// are never driven. At BUILD 0 every lane is at rest, so the signal passes
// bit for bit; pulling BUILD back to 0 is the drop.
//
// How the paths were chosen (what a producer does by hand over a 16-bar
// build, BUILD rising linearly with the bars):
//   - entries are staggered: the low end thins first, the noise comes in
//     mid-build, pitch rises late, the reverb opens in the last third and the
//     image collapses on the last bar; when everything moves at once the
//     build has nowhere to go;
//   - tension accelerates: most of the motion is in the last quarter, so
//     segments ease in (curve > 1);
//   - the kick and bass go under the low cut by mid-build, so the drop's
//     return of the low end is what hits;
//   - brightness tracks tension: the noise and the cuts move up in frequency;
//   - perceived loudness holds within a few LU of the dry loop (the gain lane
//     makes up where the cuts take more away than the noise and reverb fill).
namespace ba
{
    // A point on a lane: at BUILD `at` (0..1) the control is at `value` (a knob
    // position 0..1; semitones for pitch, dB for gain). `curve` shapes the
    // segment arriving here: 1 is straight, above 1 starts slow and lands
    // fast, below 1 the reverse.
    struct Key
    {
        float at = 0.f, value = 0.f, curve = 1.f;
    };

    // One control's path over BUILD. It holds its first value before the
    // first key and its last after the last. `step` > 0 quantises the value
    // to multiples of it (pitch stairs). Empty: the preset leaves the control
    // alone.
    struct Lane
    {
        static constexpr int kMaxKeys = 5;

        constexpr Lane() = default;
        constexpr Lane (std::initializer_list<Key> list, float quantum = 0.f) : step (quantum)
        {
            for (const auto& k : list)
            {
                if (count == kMaxKeys) throw "ba::Lane: too many keys";
                keys[(size_t) count++] = k;
            }
        }

        constexpr bool driven() const noexcept { return count > 0; }
        std::span<const Key> points() const noexcept { return { keys.data(), (size_t) count }; }

        float operator() (float build) const noexcept
        {
            if (count == 0) return 0.f;
            const float b = std::clamp (build, 0.f, 1.f);
            float v = keys[0].value;
            if (b > keys[0].at)
            {
                v = keys[(size_t) count - 1].value;
                for (size_t i = 1; i < (size_t) count; ++i)
                    if (b < keys[i].at)
                    {
                        const Key &a = keys[i - 1], &z = keys[i];
                        const float u = (b - a.at) / (z.at - a.at);
                        v = a.value + (z.value - a.value) * (z.curve == 1.f ? u : std::pow (u, z.curve));
                        break;
                    }
            }
            return step > 0.f ? step * std::floor (v / step + 1e-4f) : v;
        }

        std::array<Key, kMaxKeys> keys {};
        int count = 0;
        float step = 0.f;
    };

    struct Preset
    {
        const char* name;
        const char* section;        // menu section
        const char* help;           // one line for the editor's footer
        int lowCutType = -1;        // kLowCutCharacters; -1: the low cut stays the user's
        Lane lowCut, lowReso;       // no reso lane: auto resonance
        int highCutType = -1;       // kHighCutCharacters; -1: the high cut stays the user's
        Lane highCut, highReso;
        Lane pitch;                 // semitones
        Lane noise, noisePitch, noiseLowCut;
        Lane reverb, stereo;
        Lane gain;                  // dB on the wet signal, before dry/wet
    };

    namespace detail
    {
        template <size_t N>
        constexpr int characterIndex (const Character (&list)[N], std::string_view name)
        {
            for (size_t i = 0; i < N; ++i)
                if (name == list[i].name) return (int) i;
            throw "ba::Preset: no cut type of that name";
        }
    } // namespace detail

    // Cut types by their menu text, so the table reads like the menus.
    constexpr int lowType (std::string_view name) { return detail::characterIndex (kLowCutCharacters, name); }
    constexpr int highType (std::string_view name) { return detail::characterIndex (kHighCutCharacters, name); }

    // Knob positions used below (Mapping.h):
    //   low cut    0.20 66 Hz, 0.30 121 Hz, 0.40 221 Hz, 0.50 400 Hz, 0.60 727 Hz, 0.70 1.32 kHz
    //   high cut   0.85 9 kHz, 0.70 4.1 kHz, 0.60 2.4 kHz, 0.50 1.41 kHz, 0.40 832 Hz
    //   noise      0.35 -27 dB, 0.45 -21 dB, 0.55 -15.6 dB, 0.66 -10.8 dB, 0.8 -5.8 dB, 1 0 dB
    //   noise pitch 0.1 135 Hz, 0.3 385 Hz, 0.5 1.1 kHz, 0.7 3.1 kHz, 0.9 8.9 kHz, 1 15 kHz
    //   reverb     0.3 2.3 s, 0.5 4.3 s, 0.7 6.9 s, 1 12 s
    //   stereo     0 full width .. 1 mono
    inline constexpr Preset kPresets[] = {
        { .name = "Full-On Rise", .section = "Rise",
          .help = "The classic: lows thin out, noise rises, the reverb opens late, the image narrows.",
          .lowCutType = lowType ("Classic 24 / Rise"),
          .lowCut = { { 0.f, 0.f }, { 0.15f, 0.22f }, { 0.5f, 0.4f }, { 0.8f, 0.52f }, { 1.f, 0.64f } },
          .noise = { { 0.f, 0.f }, { 0.1f, 0.f }, { 0.4f, 0.52f }, { 0.8f, 0.68f }, { 1.f, 0.78f } },
          .noisePitch = { { 0.f, 0.3f }, { 0.4f, 0.5f }, { 1.f, 0.9f } },
          .reverb = { { 0.f, 0.f }, { 0.55f, 0.f }, { 0.85f, 0.35f }, { 1.f, 0.55f } },
          .stereo = { { 0.f, 0.f }, { 0.85f, 0.f }, { 1.f, 0.65f } },
          .gain = { { 0.f, 0.f }, { 0.25f, 0.f }, { 0.55f, 2.f }, { 0.85f, 1.f }, { 1.f, 0.5f } } },

        { .name = "Rocket", .section = "Rise",
          .help = "Hi-tech riser: the whole loop climbs an octave, slow then fast, with bright noise.",
          .lowCutType = lowType ("Hard 48 / Flay"),
          .lowCut = { { 0.f, 0.f }, { 0.3f, 0.25f }, { 1.f, 0.5f } },
          .pitch = { { 0.f, 0.f }, { 0.25f, 0.f }, { 1.f, 12.f, 2.2f } },
          .noise = { { 0.f, 0.f }, { 0.2f, 0.f }, { 0.6f, 0.55f }, { 1.f, 0.78f } },
          .noisePitch = { { 0.f, 0.55f }, { 1.f, 1.f } },
          .stereo = { { 0.f, 0.f }, { 0.75f, 0.f }, { 1.f, 0.4f } },
          .gain = { { 0.f, 0.f }, { 0.25f, 0.f }, { 0.6f, 3.f }, { 0.9f, 1.f }, { 1.f, 0.f } } },

        { .name = "Stairs", .section = "Rise",
          .help = "Goa staircase: a semitone up every sixteenth of BUILD, an octave at the top.",
          .lowCutType = lowType ("Classic 12 / Thin"),
          .lowCut = { { 0.f, 0.f }, { 0.25f, 0.15f }, { 1.f, 0.45f } },
          .pitch = { { { 0.f, 0.f }, { 0.1875f, 0.f }, { 0.9375f, 12.f }, { 1.f, 12.f } }, 1.f },
          .reverb = { { 0.f, 0.f }, { 0.75f, 0.f }, { 1.f, 0.3f } },
          .gain = { { 0.f, 0.f }, { 0.5f, 0.5f }, { 1.f, 2.5f } } },

        { .name = "Acid Scream", .section = "Rise",
          .help = "A steep, resonant low cut that screams louder the higher it climbs.",
          .lowCutType = lowType ("Classic 48 / Scream"),
          .lowCut = { { 0.f, 0.f }, { 0.1f, 0.2f }, { 1.f, 0.58f, 1.3f } },
          .lowReso = { { 0.f, 0.25f }, { 0.5f, 0.45f }, { 1.f, 0.8f } },
          .noise = { { 0.f, 0.f }, { 0.3f, 0.f }, { 1.f, 0.55f } },
          .noisePitch = { { 0.f, 0.45f }, { 1.f, 0.75f } },
          .reverb = { { 0.f, 0.f }, { 0.6f, 0.f }, { 1.f, 0.4f } },
          .gain = { { 0.f, 0.f }, { 0.3f, 0.5f }, { 0.6f, 2.f }, { 1.f, 1.5f } } },

        { .name = "Tunnel", .section = "Dark",
          .help = "The lowpass closes and the reverb grows: the music moves into the next room.",
          .highCutType = highType ("Smooth 24 / Stutter"),
          .highCut = { { 0.f, 1.f }, { 0.1f, 0.85f }, { 0.7f, 0.48f }, { 1.f, 0.34f } },
          .noise = { { 0.f, 0.f }, { 0.4f, 0.f }, { 1.f, 0.5f } },
          .noisePitch = { { 0.f, 0.25f }, { 1.f, 0.45f } },
          .reverb = { { 0.f, 0.f }, { 0.3f, 0.15f }, { 1.f, 0.6f } } },

        { .name = "Dark Forest", .section = "Dark",
          .help = "Bass carved out, top darkened, a low wind of noise and a long cavern tail.",
          .lowCutType = lowType ("Hollow 48 / Carve"),
          .lowCut = { { 0.f, 0.f }, { 0.4f, 0.25f }, { 1.f, 0.4f } },
          .highCutType = highType ("Hollow 12 / Warble"),
          .highCut = { { 0.f, 1.f }, { 0.2f, 0.85f }, { 1.f, 0.55f } },
          .noise = { { 0.f, 0.f }, { 0.15f, 0.4f }, { 1.f, 0.72f } },
          .noisePitch = { { 0.f, 0.12f }, { 0.7f, 0.3f }, { 1.f, 0.42f } },
          .noiseLowCut = { { 0.f, 0.2f } },
          .reverb = { { 0.f, 0.f }, { 0.25f, 0.2f }, { 1.f, 0.8f, 1.5f } },
          .gain = { { 0.f, 0.f }, { 0.2f, 1.5f }, { 0.6f, 3.f }, { 1.f, 1.f } } },

        { .name = "Dissolve", .section = "Wash",
          .help = "The loop dissolves into a long wash that swallows the dry signal.",
          .lowCutType = lowType ("Classic 12 / Thin"),
          .lowCut = { { 0.f, 0.f }, { 0.3f, 0.2f }, { 1.f, 0.42f } },
          .highCutType = highType ("Gentle 12 / Default"),
          .highCut = { { 0.f, 1.f }, { 0.4f, 0.92f }, { 1.f, 0.72f } },
          .reverb = { { 0.f, 0.f }, { 1.f, 1.f, 0.8f } },
          .gain = { { 0.f, 0.f }, { 0.5f, 0.5f }, { 1.f, 1.5f } } },

        { .name = "White-Out", .section = "Wash",
          .help = "The noise takes over, rumble to hiss, its own low cut climbing with it.",
          .noise = { { 0.f, 0.f }, { 0.05f, 0.35f }, { 0.7f, 0.66f }, { 1.f, 0.75f } },
          .noisePitch = { { 0.f, 0.1f }, { 1.f, 1.f } },
          .noiseLowCut = { { 0.f, 0.3f }, { 1.f, 0.7f } },
          .reverb = { { 0.f, 0.f }, { 0.3f, 0.1f }, { 1.f, 0.5f } },
          .gain = { { 0.f, 0.f }, { 0.6f, 0.f }, { 1.f, -1.5f } } },

        { .name = "Squeeze", .section = "Collapse",
          .help = "Telephone: both cuts close in on the mids while the image goes mono.",
          .lowCutType = lowType ("Hard 48 / Flay"),
          .lowCut = { { 0.f, 0.f }, { 0.1f, 0.2f }, { 1.f, 0.58f, 1.2f } },
          .highCutType = highType ("Hard 24 / Montoya"),
          .highCut = { { 0.f, 1.f }, { 0.1f, 0.85f }, { 1.f, 0.64f, 1.2f } },
          .stereo = { { 0.f, 0.f }, { 0.3f, 0.f }, { 1.f, 1.f } },
          .gain = { { 0.f, 0.f }, { 0.1f, 0.5f }, { 0.5f, 3.5f }, { 1.f, 5.5f } } },

        { .name = "Vacuum", .section = "Collapse",
          .help = "Late slam for short builds: nothing until halfway, then a sharp rise into dry mono.",
          .lowCutType = lowType ("Extreme 48 / Feed"),
          .lowCut = { { 0.f, 0.f }, { 0.55f, 0.f }, { 1.f, 0.72f, 1.8f } },
          .noise = { { 0.f, 0.f }, { 0.55f, 0.f }, { 1.f, 0.85f, 1.2f } },
          .noisePitch = { { 0.f, 0.4f }, { 0.55f, 0.4f }, { 1.f, 1.f, 1.5f } },
          .stereo = { { 0.f, 0.f }, { 0.7f, 0.f }, { 1.f, 1.f, 2.f } },
          .gain = { { 0.f, 0.f }, { 0.7f, 0.f }, { 0.85f, 3.5f }, { 1.f, 0.5f } } },
    };

    // The controls a preset can take over. A cut is one control: its
    // position, type, resonance and auto switch go together.
    enum class Control { lowCut, highCut, pitch, noise, noisePitch, noiseLowCut, reverb, stereo };

    inline bool drives (const Preset& p, Control c) noexcept
    {
        switch (c)
        {
            case Control::lowCut:      return p.lowCutType >= 0;
            case Control::highCut:     return p.highCutType >= 0;
            case Control::pitch:       return p.pitch.driven();
            case Control::noise:       return p.noise.driven();
            case Control::noisePitch:  return p.noisePitch.driven();
            case Control::noiseLowCut: return p.noiseLowCut.driven();
            case Control::reverb:      return p.reverb.driven();
            case Control::stereo:      return p.stereo.driven();
        }
        return false;
    }

    // The knobs the chain runs with: `own` (the user's), with every control
    // the preset drives set to where it is at `build` (0..1).
    inline Settings applyPreset (const Preset& p, float build, Settings s) noexcept
    {
        if (p.lowCutType >= 0)
        {
            s.lowCut = p.lowCut (build);
            s.lowCutType = p.lowCutType;
            s.lowCutAuto = ! p.lowReso.driven();
            if (! s.lowCutAuto) s.lowCutReso = p.lowReso (build);
        }
        if (p.highCutType >= 0)
        {
            s.highCut = p.highCut (build);
            s.highCutType = p.highCutType;
            s.highCutAuto = ! p.highReso.driven();
            if (! s.highCutAuto) s.highCutReso = p.highReso (build);
        }
        const auto drive = [build] (const Lane& lane, float& knob) { if (lane.driven()) knob = lane (build); };
        drive (p.pitch, s.pitch);
        drive (p.noise, s.noise);
        drive (p.noisePitch, s.noisePitch);
        drive (p.noiseLowCut, s.noiseLowCut);
        drive (p.reverb, s.reverb);
        drive (p.stereo, s.stereo);
        drive (p.gain, s.makeupDb);
        return s;
    }
} // namespace ba
