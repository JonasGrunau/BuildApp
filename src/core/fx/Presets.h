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
// are never driven. Most presets rest at BUILD 0 (the signal passes bit for
// bit, and pulling BUILD back to 0 is the drop); some start at a setting of
// their own (below). Past 75 % every preset fades towards its reverb tail
// (kPathEnd).
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
//     makes up where the cuts take more away than the noise and reverb fill);
//   - the noise sits under the music, about 10 LU under it mid-build and 2
//     to 3 LU under it at the top (White-Out, where the noise is the point,
//     reaches level). When a cut thins the music, the gain lane holds the
//     loudness, not the noise.
//
// The Progressive presets (Sunrise, Prog Roller, Inhale) break two of those
// rules on purpose, because progressive (psy)trance builds differently:
//   - the climb is even: 16 to 32 bars that keep rising instead of piling
//     into the last quarter, so curves stay near 1 and entries come early;
//   - the groove survives: a soft 12 dB low cut takes the sub and thins the
//     rolling bass but stops near 300-400 Hz, and the drop gives the low end
//     back;
//   - the lowpass opens: the classic progressive build starts muffled and
//     opens into the drop. BUILD 0 has to be at rest, so the high cut drops
//     in over the first bar and opens from there, and brightness still rises
//     with tension;
//   - the noise is air (high, its own low cut up), not a rumble;
//   - the image narrows gradually as a tension axis, and the wash is pulled
//     back on the last bar so the drop lands in a tight, dry space (the
//     breath before the drop).
//
// The Pumping Up presets (Bottom Up, Handover, Surface) do what psytrance
// does when the groove comes back after a break: it returns lowpassed, only
// the kick and bass pumping, and a steep lowpass opens from the bottom up.
// Their 0 % is that closed lowpass. It lingers low (curve > 1) and lets go
// near 20 kHz just after the low cut has started under 50 Hz, so the two
// read as one sweep that climbs on into the drop. They differ in where the
// handover falls: three quarters in (Bottom Up), halfway (Handover, then a
// full-on rise), or out of a wash (Surface).
//
// The Downlift presets go the other way, for the bars after an impact or
// into a break: their 0 % is the moment of the hit (bright noise, the loop
// an octave up, or the full mix), and BUILD takes it down: noise falling to
// a rumble, the pitch falling back, a lowpass sinking, the tail growing.
//
// Most presets rest at BUILD 0 (every lane at rest, the signal untouched
// bit for bit, so BUILD back to 0 is the drop). A preset whose 0 % is a
// setting of its own (Sunrise, the Pumping Up and some Downlift presets)
// starts there instead; switch it off to leave it.
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
    //   high cut   0.90 11.8 kHz, 0.85 9 kHz, 0.70 4.1 kHz, 0.60 2.4 kHz, 0.50 1.41 kHz, 0.40 832 Hz
    //   noise      0.3 -31 dB, 0.35 -27 dB, 0.45 -21 dB, 0.55 -15.6 dB, 0.66 -10.8 dB, 0.8 -5.8 dB, 1 0 dB
    //   noise pitch 0.1 135 Hz, 0.3 385 Hz, 0.5 1.1 kHz, 0.7 3.1 kHz, 0.9 8.9 kHz, 1 15 kHz
    //   noise cut  as the low cut: 0.5 400 Hz, 0.75 1.8 kHz
    //   reverb     0.3 2.3 s, 0.5 4.3 s, 0.7 6.9 s, 1 12 s
    //   stereo     0 full width .. 1 mono
    inline constexpr Preset kPresets[] = {
        { .name = "Full-On Rise", .section = "Rise",
          .help = "The classic: lows thin out, noise rises, the reverb opens late, the image narrows.",
          .lowCutType = lowType ("Classic 24 / Rise"),
          .lowCut = { { 0.f, 0.f }, { 0.15f, 0.22f }, { 0.5f, 0.4f }, { 0.8f, 0.52f }, { 1.f, 0.64f } },
          .noise = { { 0.f, 0.f }, { 0.1f, 0.f }, { 0.4f, 0.41f }, { 0.8f, 0.45f }, { 1.f, 0.49f } },
          .noisePitch = { { 0.f, 0.3f }, { 0.4f, 0.5f }, { 1.f, 0.9f } },
          .reverb = { { 0.f, 0.f }, { 0.55f, 0.f }, { 0.85f, 0.35f }, { 1.f, 0.55f } },
          .stereo = { { 0.f, 0.f }, { 0.85f, 0.f }, { 1.f, 0.65f } },
          .gain = { { 0.f, 0.f }, { 0.25f, 0.f }, { 0.55f, 3.4f }, { 0.85f, 3.2f }, { 1.f, 5.5f } } },

        { .name = "Rocket", .section = "Rise",
          .help = "Hi-tech riser: the whole loop climbs an octave, slow then fast, with bright noise.",
          .lowCutType = lowType ("Hard 48 / Flay"),
          .lowCut = { { 0.f, 0.f }, { 0.3f, 0.25f }, { 1.f, 0.5f } },
          .pitch = { { 0.f, 0.f }, { 0.25f, 0.f }, { 1.f, 12.f, 2.2f } },
          .noise = { { 0.f, 0.f }, { 0.2f, 0.f }, { 0.6f, 0.42f }, { 1.f, 0.53f } },
          .noisePitch = { { 0.f, 0.55f }, { 1.f, 1.f } },
          .stereo = { { 0.f, 0.f }, { 0.75f, 0.f }, { 1.f, 0.4f } },
          .gain = { { 0.f, 0.f }, { 0.25f, 0.f }, { 0.6f, 3.4f }, { 1.f, 2.8f } } },

        { .name = "Stairs", .section = "Rise",
          .help = "Goa staircase: the loop climbs a semitone at a time, an octave at the top.",
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
          .noise = { { 0.f, 0.f }, { 0.15f, 0.35f }, { 1.f, 0.56f } },
          .noisePitch = { { 0.f, 0.12f }, { 0.7f, 0.3f }, { 1.f, 0.42f } },
          .noiseLowCut = { { 0.f, 0.2f } },
          .reverb = { { 0.f, 0.f }, { 0.25f, 0.2f }, { 1.f, 0.8f, 1.5f } },
          .gain = { { 0.f, 0.f }, { 0.2f, 1.5f }, { 0.5f, 4.f }, { 0.8f, 3.5f }, { 1.f, 1.f } } },

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
          .noise = { { 0.f, 0.f }, { 0.55f, 0.f }, { 1.f, 0.48f, 0.8f } },
          .noisePitch = { { 0.f, 0.4f }, { 0.55f, 0.4f }, { 1.f, 1.f, 1.5f } },
          .stereo = { { 0.f, 0.f }, { 0.7f, 0.f }, { 1.f, 1.f, 2.f } },
          .gain = { { 0.f, 0.f }, { 0.7f, 0.f }, { 0.85f, 3.2f }, { 1.f, 5.5f } } },

        { .name = "Sunrise", .section = "Progressive",
          .help = "Starts under a lowpass that opens across the build while the lows thin away.",
          .lowCutType = lowType ("Gentle 12 / Soft"),
          .lowCut = { { 0.f, 0.f }, { 0.25f, 0.12f }, { 1.f, 0.48f, 1.4f } },
          .highCutType = highType ("Classic 48 / Gate"),
          .highCut = { { 0.f, 0.42f }, { 1.f, 0.92f } },
          .highReso = { { 0.f, 0.35f }, { 1.f, 0.55f } },
          .noise = { { 0.f, 0.f }, { 0.25f, 0.f }, { 1.f, 0.5f } },
          .noisePitch = { { 0.f, 0.45f }, { 1.f, 0.85f } },
          .gain = { { 0.f, 0.f }, { 0.4f, 0.f }, { 1.f, 3.5f } } },

        { .name = "Prog Roller", .section = "Progressive",
          .help = "The bass keeps rolling under a soft low cut; air rises, the image narrows.",
          .lowCutType = lowType ("Smooth 12 / Pump"),
          .lowCut = { { 0.f, 0.f }, { 0.05f, 0.15f }, { 0.75f, 0.36f }, { 1.f, 0.46f } },
          .noise = { { 0.f, 0.f }, { 0.1f, 0.3f }, { 1.f, 0.58f } },
          .noisePitch = { { 0.f, 0.7f }, { 1.f, 0.88f } },
          .stereo = { { 0.f, 0.f }, { 0.25f, 0.f }, { 1.f, 0.55f } },
          .gain = { { 0.f, 0.f }, { 0.06f, -1.5f }, { 0.45f, 0.f }, { 1.f, 2.5f } } },

        { .name = "Inhale", .section = "Progressive",
          .help = "An airy wash swells across the build; the last bar breathes it in: dry, thin, narrow.",
          .lowCutType = lowType ("Clean 48 / Steep"),
          .lowCut = { { 0.f, 0.f }, { 0.2f, 0.15f }, { 0.9f, 0.32f }, { 1.f, 0.62f, 1.5f } },
          .noise = { { 0.f, 0.f }, { 0.1f, 0.3f }, { 0.8f, 0.46f }, { 1.f, 0.44f } },
          .noisePitch = { { 0.f, 0.6f }, { 1.f, 0.95f } },
          .noiseLowCut = { { 0.f, 0.5f }, { 1.f, 0.75f } },
          .reverb = { { 0.f, 0.f }, { 0.1f, 0.15f }, { 0.85f, 0.75f, 0.9f }, { 1.f, 0.2f } },
          .stereo = { { 0.f, 0.f }, { 0.9f, 0.f }, { 1.f, 0.85f, 1.5f } },
          .gain = { { 0.f, 0.f }, { 0.25f, 1.5f }, { 0.6f, 3.5f }, { 0.85f, 3.5f }, { 1.f, 7.5f } } },

        { .name = "Bottom Up", .section = "Pumping Up",
          .help = "The groove pumps up from the bottom as the lowpass opens; the low cut takes over.",
          .lowCutType = lowType ("Smooth 12 / Pump"),
          .lowCut = { { 0.f, 0.f }, { 0.68f, 0.f }, { 1.f, 0.55f } },
          .highCutType = highType ("Classic 48 / Gate"),
          .highCut = { { 0.f, 0.12f }, { 0.75f, 1.f, 1.8f } },
          .highReso = { { 0.f, 0.3f }, { 1.f, 0.4f } },
          .stereo = { { 0.f, 0.6f }, { 0.75f, 0.f }, { 1.f, 0.3f } },
          .gain = { { 0.f, -2.5f }, { 0.6f, -1.8f }, { 0.78f, -3.f }, { 1.f, 5.5f } } },

        { .name = "Handover", .section = "Pumping Up",
          .help = "The lowpass opens over the first half, then the low cut rises into a full-on build.",
          .lowCutType = lowType ("Classic 24 / Rise"),
          .lowCut = { { 0.f, 0.f }, { 0.4f, 0.f }, { 1.f, 0.64f } },
          .highCutType = highType ("Hard 48 / Drive"),
          .highCut = { { 0.f, 0.1f }, { 0.5f, 1.f } },
          .noise = { { 0.f, 0.f }, { 0.45f, 0.f }, { 0.8f, 0.4f }, { 1.f, 0.47f } },
          .noisePitch = { { 0.f, 0.4f }, { 1.f, 0.9f } },
          .reverb = { { 0.f, 0.f }, { 0.6f, 0.f }, { 1.f, 0.45f } },
          .stereo = { { 0.f, 0.f }, { 0.85f, 0.f }, { 1.f, 0.6f } },
          .gain = { { 0.f, -1.f }, { 0.5f, -1.2f }, { 0.75f, 2.f }, { 1.f, 6.f } } },

        { .name = "Surface", .section = "Pumping Up",
          .help = "Up from underwater: a steep, washed lowpass opens and dries out, then the lows thin.",
          .lowCutType = lowType ("Classic 12 / Thin"),
          .lowCut = { { 0.f, 0.f }, { 0.5f, 0.f }, { 1.f, 0.45f } },
          .highCutType = highType ("Classic 48 / Gate"),
          .highCut = { { 0.f, 0.18f }, { 0.6f, 1.f, 1.8f } },
          .reverb = { { 0.f, 0.45f }, { 0.6f, 0.1f }, { 1.f, 0.35f } },
          .gain = { { 0.f, -1.f }, { 0.3f, -3.3f }, { 0.55f, -0.8f }, { 1.f, 5.f } } },

        { .name = "Downsweep", .section = "Downlift",
          .help = "Bright noise falls to a rumble and fades while the top closes and the tail grows.",
          .highCutType = highType ("Classic 12 / Vintage"),
          .highCut = { { 0.f, 1.f }, { 1.f, 0.55f, 0.7f } },
          .noise = { { 0.f, 0.48f }, { 1.f, 0.15f, 0.7f } },
          .noisePitch = { { 0.f, 0.95f }, { 1.f, 0.1f, 0.5f } },
          .noiseLowCut = { { 0.f, 0.6f }, { 1.f, 0.05f, 0.5f } },
          .reverb = { { 0.f, 0.25f }, { 1.f, 0.6f } },
          .gain = { { 0.f, 0.f }, { 1.f, 1.f } } },

        { .name = "Freefall", .section = "Downlift",
          .help = "The loop starts an octave up and falls back down, faster and faster, under a closing top.",
          .highCutType = highType ("Hard 24 / Montoya"),
          .highCut = { { 0.f, 1.f }, { 0.3f, 0.9f }, { 1.f, 0.45f, 1.3f } },
          .pitch = { { 0.f, 12.f }, { 1.f, 0.f, 1.6f } },
          .noise = { { 0.f, 0.45f }, { 0.4f, 0.2f }, { 1.f, 0.f } },
          .noisePitch = { { 0.f, 0.9f }, { 1.f, 0.2f } },
          .reverb = { { 0.f, 0.15f }, { 1.f, 0.55f } },
          .gain = { { 0.f, 0.f }, { 1.f, 1.f } } },

        { .name = "Sink", .section = "Downlift",
          .help = "A steep lowpass sinks the music down to its sub while a rumble falls and the tail grows.",
          .highCutType = highType ("Classic 48 / Gate"),
          .highCut = { { 0.f, 1.f }, { 0.1f, 0.85f }, { 1.f, 0.08f, 0.8f } },
          .noise = { { 0.f, 0.f }, { 0.15f, 0.4f }, { 1.f, 0.5f } },
          .noisePitch = { { 0.f, 0.5f }, { 1.f, 0.05f } },
          .noiseLowCut = { { 0.f, 0.f } },
          .reverb = { { 0.f, 0.f }, { 0.2f, 0.25f }, { 1.f, 0.7f } },
          .stereo = { { 0.f, 0.f }, { 0.4f, 0.f }, { 1.f, 0.6f } },
          .gain = { { 0.f, 0.f }, { 1.f, 1.f } } },
    };

    // Past the path, BUILD takes every preset further, towards nothing. A
    // preset's lanes play over BUILD 0..kPathEnd (the path, 0..1 in the
    // lanes' own terms), where every control reaches its top. Then the reverb
    // comes up to at least kTailReverb (by kFadeFrom), so there is a tail,
    // and only then does the level go: evenly in dB, from wherever the
    // preset's gain lane left it down to kFadeToDb at 100 %. The fade sits
    // before the reverb, so the tail follows the music down.
    inline constexpr float kPathEnd = 0.75f, kFadeFrom = 0.8f, kTailReverb = 0.55f, kFadeToDb = -20.f;

    // The controls a preset can take over. A cut is one control: its
    // position, type, resonance and auto switch go together. Past the path
    // every preset takes the reverb too, for the tail.
    enum class Control { lowCut, highCut, pitch, noise, noisePitch, noiseLowCut, reverb, stereo };

    inline bool drives (const Preset& p, Control c, float build = 0.f) noexcept
    {
        switch (c)
        {
            case Control::lowCut:      return p.lowCutType >= 0;
            case Control::highCut:     return p.highCutType >= 0;
            case Control::pitch:       return p.pitch.driven();
            case Control::noise:       return p.noise.driven();
            case Control::noisePitch:  return p.noisePitch.driven();
            case Control::noiseLowCut: return p.noiseLowCut.driven();
            case Control::reverb:      return p.reverb.driven() || build > kPathEnd;
            case Control::stereo:      return p.stereo.driven();
        }
        return false;
    }

    // How much of each lane's path lands in the output, 0..1 (1: the preset as
    // designed). A lane at intensity k sits k of the way from where its
    // control rests by default to where the preset takes it (the noise: k of
    // its amplitude): low cut, high cut, pitch, noise, reverb and stereo
    // towards rest (off), noise pitch and
    // the noise's low cut towards their defaults (1.1 kHz, 200 Hz), a
    // resonance towards auto resonance, level (the gain lane and the fade into
    // the tail) towards 0 dB. Pitch stairs stay on whole semitones. With no
    // reverb lane, the reverb row is your knob plus the tail, and only the
    // tail scales. At 0 a lane is at rest, so a preset that rests at BUILD 0
    // still does at any intensity.
    struct Intensity
    {
        float lowCut = 1.f, lowReso = 1.f, highCut = 1.f, highReso = 1.f, pitch = 1.f, noise = 1.f, noisePitch = 1.f,
              noiseLowCut = 1.f, reverb = 1.f, stereo = 1.f, level = 1.f;

        constexpr bool asDesigned() const noexcept
        {
            return lowCut == 1.f && lowReso == 1.f && highCut == 1.f && highReso == 1.f && pitch == 1.f && noise == 1.f
                   && noisePitch == 1.f && noiseLowCut == 1.f && reverb == 1.f && stereo == 1.f && level == 1.f;
        }
        static constexpr Intensity none() noexcept { return { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f }; }
    };

    // The knobs the chain runs with: `own` (the user's), with every control
    // the preset drives set to where it is at `build` (0..1), each lane at
    // its intensity.
    inline Settings applyPreset (const Preset& p, float build, Settings s, const Intensity& k = {}) noexcept
    {
        const float ownReverb = s.reverb;
        const float at = std::min (std::max (build, 0.f) / kPathEnd, 1.f);   // along the path
        if (p.lowCutType >= 0)
        {
            s.lowCut = p.lowCut (at);
            s.lowCutType = p.lowCutType;
            s.lowCutAuto = ! p.lowReso.driven();
            if (! s.lowCutAuto) s.lowCutReso = p.lowReso (at);
        }
        if (p.highCutType >= 0)
        {
            s.highCut = p.highCut (at);
            s.highCutType = p.highCutType;
            s.highCutAuto = ! p.highReso.driven();
            if (! s.highCutAuto) s.highCutReso = p.highReso (at);
        }
        const auto drive = [at] (const Lane& lane, float& knob) { if (lane.driven()) knob = lane (at); };
        drive (p.pitch, s.pitch);
        drive (p.noise, s.noise);
        drive (p.noisePitch, s.noisePitch);
        drive (p.noiseLowCut, s.noiseLowCut);
        drive (p.reverb, s.reverb);
        drive (p.stereo, s.stereo);
        drive (p.gain, s.makeupDb);

        if (build > kPathEnd)
        {
            const float up = std::min ((build - kPathEnd) / (kFadeFrom - kPathEnd), 1.f);
            s.reverb += (std::max (s.reverb, kTailReverb) - s.reverb) * up;
            if (build > kFadeFrom)
            {
                const float down = std::min ((build - kFadeFrom) / (1.f - kFadeFrom), 1.f);
                s.fadeDb = std::min (0.f, kFadeToDb - s.makeupDb) * down;
            }
        }
        if (k.asDesigned()) return s;

        const Settings rest;
        const auto scale = [] (float& v, float base, float amount) { if (amount != 1.f) v = base + (v - base) * amount; };
        // A resonance lane goes towards what auto resonance does at the
        // (scaled) cutoff; at 0 it is auto again.
        const auto reso = [] (bool lowpass, float& v, bool& autoOn, CutKnobs knobs, float amount) {
            if (amount == 1.f || autoOn) return;
            if (amount <= 0.f) { autoOn = true; return; }
            knobs.autoReso = true;
            const float a = (float) CutFilters::resonance (lowpass, knobs);
            v = a + (v - a) * amount;
        };
        if (p.lowCutType >= 0)
        {
            scale (s.lowCut, rest.lowCut, k.lowCut);
            reso (false, s.lowCutReso, s.lowCutAuto, s.lowCutKnobs(), k.lowReso);
        }
        if (p.highCutType >= 0)
        {
            scale (s.highCut, rest.highCut, k.highCut);
            reso (true, s.highCutReso, s.highCutAuto, s.highCutKnobs(), k.highReso);
        }
        if (p.pitch.driven() && k.pitch != 1.f)
        {
            s.pitch *= k.pitch;
            if (p.pitch.step > 0.f) s.pitch = p.pitch.step * std::floor (s.pitch / p.pitch.step + 1e-4f);
        }
        // The noise scales in amplitude (half is -6 dB), not along its knob's
        // audio taper, where a quarter would already be -36 dB.
        if (p.noise.driven() && k.noise != 1.f) s.noise *= std::cbrt (std::max (k.noise, 0.f));
        if (p.noisePitch.driven()) scale (s.noisePitch, rest.noisePitch, k.noisePitch);
        if (p.noiseLowCut.driven()) scale (s.noiseLowCut, rest.noiseLowCut, k.noiseLowCut);
        scale (s.reverb, p.reverb.driven() ? rest.reverb : ownReverb, k.reverb);
        if (p.stereo.driven()) scale (s.stereo, rest.stereo, k.stereo);
        scale (s.makeupDb, 0.f, k.level);
        scale (s.fadeDb, 0.f, k.level);
        return s;
    }

    // Whether BUILD 0 leaves the signal alone (every lane at rest there), so
    // pulling BUILD back to 0 is the drop. The others start at a setting of
    // their own.
    inline bool restsAtZero (const Preset& p) noexcept
    {
        const auto s = applyPreset (p, 0.f, Settings {});
        return map::lowCutOff (s.lowCut) && map::highCutOff (s.highCut) && s.pitch == 0.f && s.noise == 0.f && s.reverb == 0.f
               && s.stereo == 0.f && s.makeupDb == 0.f;
    }
} // namespace ba
