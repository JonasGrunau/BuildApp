// Renders the editor offscreen to PNGs, so the UI can be reviewed without a DAW.
//   buildapp_snapshot <out-dir> [--scale=1] [state ...]   (2x unless --scale)
// States (default: all but bench), each with the built-in psytrance loop
// playing through for three seconds:
//   rest     every knob at rest
//   build    halfway into a buildup: cuts closing in, +4 st, noise, reverb
//   peak     the bar before the drop: everything far up
//   cuts     both cuts on, nothing else
//   types    both cuts on in other types (Classic 48 / Scream, Hollow 12 / Warble),
//            the high cut's resonance set by hand
//   rise     preset Full-On Rise at BUILD 70 %: low cut, noise, reverb, stereo locked
//   squeeze  preset Squeeze at BUILD 90 %: both cuts and stereo locked, mono
//   vacuum   preset Vacuum at BUILD 40 %: locked, nothing moving yet
//   steps    rise, with the mouse on the preset's > key and the high cut's
//            < key held down (it wraps to the last type); the low cut's keys dimmed
//   bench    times the spectrum and whole-editor paints instead

#include "TestUtil.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

namespace
{
    struct Knobs
    {
        float lowCut = 0.f, highCut = 1.f, pitch = 0.f, noise = 0.f, noisePitch = 0.5f, reverb = 0.f, stereo = 0.f, level = 0.f, mix = 100.f;
        int lowType = 0, highType = 0;
        bool highAuto = true;
        float highReso = 30.f;
        int preset = 0;   // 1-based, 0 = off
        float build = 0.f;
    };

    void set (BuildAppProcessor& p, const char* id, float plain)
    {
        auto& prm = p.param (id);
        prm.setValueNotifyingHost (prm.convertTo0to1 (plain));
    }

    void apply (BuildAppProcessor& p, const Knobs& k)
    {
        set (p, ids::lowCut, k.lowCut);
        set (p, ids::highCut, k.highCut);
        set (p, ids::pitch, k.pitch);
        set (p, ids::noise, k.noise);
        set (p, ids::noisePitch, k.noisePitch);
        set (p, ids::reverb, k.reverb);
        set (p, ids::stereo, k.stereo);
        set (p, ids::level, k.level);
        set (p, ids::mix, k.mix);
        set (p, ids::lowCutType, (float) k.lowType);
        set (p, ids::highCutType, (float) k.highType);
        set (p, ids::highCutAuto, k.highAuto ? 1.f : 0.f);
        set (p, ids::highCutReso, k.highReso);
        set (p, ids::preset, (float) k.preset);
        set (p, ids::build, k.build);
    }

    void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    // The mouse over a point of a component, optionally pressed there.
    void pointAt (juce::Component& c, juce::Point<float> p, bool press)
    {
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), p, {}, 0.f, 0.f, 0.f, 0.f, 0.f, &c, &c, now, p, now, 1, false);
        c.mouseMove (e);
        if (press) c.mouseDown (e);
    }

    std::vector<TypeSelector*> typeSelectors (juce::Component& root)   // preset, low cut, high cut
    {
        std::vector<TypeSelector*> out;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (auto* t = dynamic_cast<TypeSelector*> (child)) out.push_back (t);
                walk (*child);
            }
        };
        walk (root);
        return out;
    }

    // Plays `loop` through the processor, ticking the editor about 30 times a second.
    void play (BuildAppProcessor& p, BuildAppEditor& ed, const testutil::Stereo& loop, double fs, double seconds)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        const auto total = (size_t) (seconds * fs);
        size_t sinceTick = 0;
        for (size_t s = 0; s < total; s += 512)
        {
            const int n = (int) std::min<size_t> (512, total - s);
            buf.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                const size_t at = (s + (size_t) i) % loop.size();
                buf.setSample (0, i, loop.left[at]);
                buf.setSample (1, i, loop.right[at]);
            }
            p.processBlock (buf, midi);
            if ((sinceTick += (size_t) n) >= (size_t) (fs / 30.0))
            {
                sinceTick = 0;
                ed.tick();
            }
        }
    }

    double timePaint (juce::Component& c, int runs)
    {
        juce::Image img (juce::Image::ARGB, c.getWidth() * 2, c.getHeight() * 2, true, juce::NativeImageType());
        const auto once = [&] {
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale (2.f));
            c.paintEntireComponent (g, false);
        };
        once();
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < runs; ++i) once();
        return (juce::Time::getMillisecondCounterHiRes() - t0) / runs;
    }
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    juce::StringArray states;
    float scale = 2.f;   // --scale=1 renders at 1x
    for (int i = 2; i < argc; ++i)
        if (const juce::String a (argv[i]); a.startsWith ("--scale=")) scale = a.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        else states.add (a);
    if (states.isEmpty()) states = { "rest", "build", "peak", "cuts", "types", "rise", "squeeze", "vacuum", "steps" };

    const double fs = 48000.0;
    const auto loop = testutil::psyLoop (fs, 60.0 / 145.0 * 8.0);   // two bars

    for (const auto& state : states)
    {
        Knobs k;
        if (state == "build")
            k = { 0.38f, 0.78f, 4.f, 0.55f, 0.6f, 0.45f, 0.25f, 0.f, 100.f };
        else if (state == "peak" || state == "bench")
            k = { 0.6f, 0.62f, 11.f, 0.85f, 0.88f, 0.9f, 0.7f, 2.f, 100.f };
        else if (state == "cuts")
            k = { 0.3f, 0.7f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.f, 100.f };
        else if (state == "types")
            k = { 0.3f, 0.7f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.f, 100.f, 1, 3, false, 22.f };
        else if (state == "rise" || state == "steps")
            k = { .preset = 1, .build = 70.f };
        else if (state == "squeeze")
            k = { .preset = 9, .build = 90.f };
        else if (state == "vacuum")
            k = { .preset = 10, .build = 40.f };

        BuildAppProcessor proc;
        apply (proc, k);
        proc.prepareToPlay (fs, 512);
        std::unique_ptr<BuildAppEditor> editor (dynamic_cast<BuildAppEditor*> (proc.createEditor()));
        pump (50);
        play (proc, *editor, loop, fs, 3.0);
        pump (50);

        if (state == "steps")
        {
            // The keys end 4 px before the selector's right edge: > centred 10.5 px in, < 16 px further.
            const auto sel = typeSelectors (*editor);
            pointAt (*sel[0], { (float) sel[0]->getWidth() - 10.5f, 8.5f }, false);
            pointAt (*sel[2], { (float) sel[2]->getWidth() - 26.5f, 8.5f }, true);
            editor->tick();
            pump (50);
        }

        if (state == "bench")
        {
            std::printf ("whole editor paint: %6.2f ms\n", timePaint (*editor, 20));
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 60; ++i) editor->tick();
            std::printf ("UI tick (no audio): %6.3f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 60.0);
            continue;
        }

        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        const auto file = outDir.getChildFile ("buildapp_" + state + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("%s -> %s\n", state.toRawUTF8(), file.getFullPathName().toRawUTF8());
    }
    return 0;
}
