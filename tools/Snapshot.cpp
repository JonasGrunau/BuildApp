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
//   sunrise  preset Sunrise at BUILD 50 %: the lowpass it starts under, opening,
//            its resonance driven
//   pumping  preset Bottom Up at BUILD 30 %: the steep lowpass still low, the
//            low cut waiting for the handover
//   tail     preset Full-On Rise at BUILD 92 %: past the path, the level fading
//            towards -20 dB (the reverb knob locked for the tail)
//   downlift preset Downsweep at BUILD 25 %: the noise falling from the hit
//   steps    rise, with the mouse on the preset's > key and the high cut's
//            < key held down (it wraps to the last type); the low cut's keys dimmed
//   intensity rise with the low cut at 50 %, the noise at 25 % and the stereo at
//            0 % intensity, the mouse on the noise's knob (its help in the footer)
//   scream   preset Acid Scream at BUILD 60 %, its resonance at 30 % intensity
//            (resting on auto resonance) and the reverb at 0 %
//   switched intensity, then the preset's keys: < to Off and > back (the
//            intensities stay), > again to the next preset (all back to 100 %);
//            prints what the intensities were after each step
//   edit     the preset editor (its window's content) on Full-On Rise at
//            BUILD 50 %, the mouse on a low cut key (its value box)
//   edit-fork that key dragged up: the preset became "Full-On Rise copy", unsaved
//   edit-draw Draw Mode on Stairs: steps drawn into the pitch lane on the 1/16 grid
//   edit-select a time selection across the low cut and noise rows, the mouse
//            in it on the noise row (its handles)
//   edit-bend a low cut segment Option-dragged, mid-bend
//   edit-new PRESET off: the editor with no lanes yet
//   edit-small Full-On Rise at the editor's smallest size
//   bench    times the spectrum and whole-editor paints instead
//   edit-bench times the preset editor's paints on Full-On Rise
//   selftest no picture: the user presets end to end (a factory preset copied
//            by its first edit, undo and redo, the project state and what it
//            plays, the library, a project from before more factory presets);
//            prints each check, exits non-zero when one fails

#include "TestUtil.h"
#include "fx/PresetText.h"
#include "plugin/Format.h"
#include "ui/PresetMenu.h"
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
        std::vector<std::pair<const char*, float>> intensity {};   // percent, by parameter id
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
        for (const auto& [id, percent] : k.intensity) set (p, id, percent);
    }

    template <typename T>
    std::vector<T*> childrenOf (juce::Component& root)
    {
        std::vector<T*> out;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (auto* t = dynamic_cast<T*> (child)) out.push_back (t);
                walk (*child);
            }
        };
        walk (root);
        return out;
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

    std::vector<TypeSelector*> typeSelectors (juce::Component& root) { return childrenOf<TypeSelector> (root); }   // preset, low cut, high cut

    // A mouse event on c at p (with modifiers; pressed at `down`).
    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = {}, juce::Point<float> down = {}, bool dragged = false)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.f, 0.f, 0.f, 0.f, 0.f, &c, &c, now, down, now, 1, dragged };
    }

    // The loop through a processor for `seconds`, its output.
    testutil::Stereo render (BuildAppProcessor& p, const testutil::Stereo& loop, double fs, double seconds)
    {
        testutil::Stereo out;
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        const auto total = (size_t) (seconds * fs);
        for (size_t s = 0; s < total; s += 512)
        {
            const int n = (int) std::min<size_t> (512, total - s);
            buf.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, loop.left[(s + (size_t) i) % loop.size()]);
                buf.setSample (1, i, loop.right[(s + (size_t) i) % loop.size()]);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < n; ++i)
            {
                out.left.push_back (buf.getSample (0, i));
                out.right.push_back (buf.getSample (1, i));
            }
        }
        return out;
    }

    void editorState (const juce::String& state, PresetEditorView& view);

    int failures = 0;
    void check (bool ok, const char* what)
    {
        std::printf ("selftest: %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok) ++failures;
    }

    // The user presets end to end (see the top).
    void selfTest (const testutil::Stereo& loop, double fs)
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("buildapp-selftest-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        folder.createDirectory();
       #if JUCE_WINDOWS
        _putenv_s ("BUILDAPP_PRESET_FOLDER", folder.getFullPathName().toRawUTF8());
       #else
        setenv ("BUILDAPP_PRESET_FOLDER", folder.getFullPathName().toRawUTF8(), 1);
       #endif
        using P = BuildAppProcessor;

        BuildAppProcessor a;
        set (a, ids::preset, 1.f);
        set (a, ids::build, 60.f);
        a.prepareToPlay (fs, 512);
        {
            PresetEditorView view (a);
            view.setSize (PresetEditorView::kWidth, PresetEditorView::kHeight);
            view.tick();
            pump (50);
            editorState ("edit-fork", view);   // drags a low cut key up
            const int slot = P::slotOf (a.presetIndex());
            check (slot >= 0, "the first edit of a factory preset turns PRESET to a user slot");
            check (slot >= 0 && a.user[slot].name == "Full-On Rise copy" && a.user[slot].unsaved && a.user[slot].copiedFrom == 1,
                   "the copy is called Full-On Rise copy, unsaved, copied from preset 1");
            check (slot >= 0 && ba::toText (a.user[slot].path) != ba::toText (ba::kPresets[0]), "the copy has the edit, the factory preset doesn't");

            view.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
            view.tick();
            check (a.presetIndex() == 1 && ! a.user[slot].used, "undo takes PRESET back to the factory preset and empties the slot");
            view.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
            view.tick();
            check (a.presetIndex() == P::presetOf (slot) && a.user[slot].used && a.user[slot].name == "Full-On Rise copy", "redo brings the copy back");
        }
        const int slot = P::slotOf (a.presetIndex());

        // The project: another instance loads it and plays it the same.
        juce::MemoryBlock state;
        a.getStateInformation (state);
        BuildAppProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        b.prepareToPlay (fs, 512);
        a.reset();
        a.prepareToPlay (fs, 512);
        check (b.presetIndex() == a.presetIndex() && slot >= 0 && b.user[slot].used && b.user[slot].name == a.user[slot].name
                   && ba::toText (b.user[slot].path) == ba::toText (a.user[slot].path) && b.user[slot].unsaved,
               "the project keeps the user preset: slot, name, path, unsaved");
        const auto ya = render (a, loop, fs, 1.0), yb = render (b, loop, fs, 1.0);
        check (ya.left == yb.left && ya.right == yb.right, "the reloaded project plays it bit for bit the same");
        BuildAppProcessor off;
        off.prepareToPlay (fs, 512);
        check (ya.left != render (off, loop, fs, 1.0).left, "and that isn't the loop untouched (PRESET off)");

        // The library: save it, another instance lists and loads it.
        auto saved = a.user[slot];
        saved.name = "Selftest Rise";
        saved.file = UserPresets::fileFor (saved.name);
        saved.savedHash = UserPresets::hashOf (saved);
        check (UserPresets::write (saved.file, saved), "a preset file is written");
        a.user.set (slot, saved);
        check (! a.user[slot].unsaved, "saved, it isn't unsaved any more");
        const auto library = UserPresets::library();
        check (library.size() == 1 && library[0].name == "Selftest Rise", "the library lists it by name");
        BuildAppProcessor c;
        c.prepareToPlay (fs, 512);
        check (! library.empty() && presetmenu::load (c, library[0].file, {}), "another instance loads it from the menu");
        const int cs = P::slotOf (c.presetIndex());
        check (cs >= 0 && ba::toText (c.user[cs].path) == ba::toText (saved.path) && ! c.user[cs].unsaved && c.user[cs].file == saved.file,
               "it plays the saved path, saved, from its file");
        check (presetmenu::freeName (c, "Selftest Rise") == "Selftest Rise 2", "a new name never takes one that's there");
        const int fresh = presetmenu::newPreset (c);
        check (fresh >= 0 && fresh != cs && c.user[fresh].name == "New preset" && c.presetIndex() == P::presetOf (fresh) && c.activePreset() != nullptr,
               "New preset makes an empty one in a free slot and turns it on");

        // A project saved when there were fewer factory presets: its user slot moves up with them.
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            tree.getChildWithName ("UserPresets").setProperty ("factoryCount", P::kFactory - 1, nullptr);
            for (auto param : tree)
                if (param.getProperty ("id").toString() == ids::preset) param.setProperty ("value", a.presetIndex() - 1, nullptr);
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
            BuildAppProcessor d;
            d.setStateInformation (old.getData(), (int) old.getSize());
            check (d.presetIndex() == a.presetIndex() && d.user[slot].used, "a project from before a new factory preset finds its user preset");
        }
        folder.deleteRecursively();
    }

    // The preset editor driven the way the edit* states say (see the top).
    void editorState (const juce::String& state, PresetEditorView& view)
    {
        auto& lanes = view.laneEditor();
        // Where a lane value sits on a row of the lane editor (as LaneEditor lays it out).
        const auto spot = [&lanes] (int row, float path, float value, ba::LaneRange r = {}) {
            const float plotX = LaneEditor::kPad, plotW = (float) lanes.getWidth() - 2.f * LaneEditor::kPad - LaneEditor::kValueW;
            const float top = lanes.rowTop (row) + 8.f, h = lanes.rowHeight() - 16.f;
            return juce::Point<float> (plotX + plotW * path * ba::kPathEnd, top + h * (1.f - r.norm (value)));
        };
        const auto drag = [&lanes] (juce::Point<float> from, std::initializer_list<juce::Point<float>> path, juce::ModifierKeys mods, bool release) {
            lanes.mouseMove (mouse (lanes, from, mods));
            lanes.mouseDown (mouse (lanes, from, mods, from));
            for (const auto& p : path) lanes.mouseDrag (mouse (lanes, p, mods, from, true));
            if (release) lanes.mouseUp (mouse (lanes, path.size() > 0 ? *(path.end() - 1) : from, mods, from, path.size() > 0));
        };
        const auto key = spot (0, 0.5f, 0.4f);   // Full-On Rise's low cut, its third key
        if (state == "edit" || state == "edit-small") lanes.mouseMove (mouse (lanes, key));
        else if (state == "edit-fork")
        {
            drag (key, { key.translated (0.f, -6.f), key.translated (0.f, -14.f) }, {}, true);
            lanes.mouseMove (mouse (lanes, key.translated (0.f, -14.f)));
        }
        else if (state == "edit-draw")
        {
            lanes.setDrawMode (true);
            const ba::LaneRange st { 0.f, 12.f };
            drag (spot (1, 0.3f, 2.f, st), { spot (1, 0.36f, 3.f, st), spot (1, 0.42f, 5.f, st), spot (1, 0.49f, 5.f, st), spot (1, 0.55f, 8.f, st),
                                             spot (1, 0.61f, 10.f, st) },
                  {}, true);
            lanes.mouseMove (mouse (lanes, spot (1, 0.7f, 9.f, st)));
        }
        else if (state == "edit-select")
        {
            drag (spot (1, 0.25f, 0.95f), { spot (0, 0.5f, 0.95f), spot (0, 0.625f, 0.95f) }, {}, true);
            lanes.mouseMove (mouse (lanes, spot (1, 0.45f, 0.9f)));
        }
        else if (state == "edit-bend")
        {
            const auto on = spot (0, 0.65f, 0.46f);
            drag (on, { on.translated (0.f, -8.f), on.translated (0.f, -18.f) }, juce::ModifierKeys::altModifier, false);
        }
        view.tick();
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
    if (states.isEmpty())
        states = { "rest", "build", "peak", "cuts", "types", "rise", "squeeze", "vacuum", "sunrise", "pumping", "tail", "downlift", "steps",
                   "intensity", "scream", "switched", "edit", "edit-fork", "edit-draw", "edit-select", "edit-bend", "edit-new", "edit-small" };

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
        else if (state == "rise" || state == "steps" || state == "edit" || state == "edit-fork" || state == "edit-select" || state == "edit-bend"
                 || state == "edit-small" || state == "edit-bench")
            k = { .preset = 1, .build = 70.f };
        else if (state == "squeeze")
            k = { .preset = 9, .build = 90.f };
        else if (state == "vacuum")
            k = { .preset = 10, .build = 40.f };
        else if (state == "sunrise")
            k = { .preset = 11, .build = 50.f };
        else if (state == "pumping")
            k = { .preset = 14, .build = 30.f };
        else if (state == "tail")
            k = { .preset = 1, .build = 92.f };
        else if (state == "downlift")
            k = { .preset = 17, .build = 25.f };
        else if (state == "intensity" || state == "switched")
            k = { .preset = 1, .build = 70.f,
                  .intensity = { { ids::lowCutIntensity, 50.f }, { ids::noiseIntensity, 25.f }, { ids::stereoIntensity, 0.f } } };
        else if (state == "edit-draw")
            k = { .preset = 3, .build = 40.f };
        else if (state == "scream")
            k = { .preset = 4, .build = 60.f, .intensity = { { ids::lowResoIntensity, 30.f }, { ids::reverbIntensity, 0.f } } };

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

        if (state == "intensity")
            for (auto* knob : childrenOf<SmallDial> (*editor))
                if (knob->isVisible() && knob->getHelp().startsWith ("Noise intensity"))
                {
                    pointAt (*knob, knob->getLocalBounds().getCentre().toFloat(), false);
                    knob->mouseEnter ({ juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.f, 0.f, 0.f, 0.f, 0.f, knob, knob,
                                        juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false });
                    editor->tick();
                    pump (50);
                }

        if (state == "switched")
        {
            const auto intensities = [&] {
                const auto now = proc.intensity();
                return fmt::fixed (now.lowCut * 100.0, 0) + "/" + fmt::fixed (now.noise * 100.0, 0) + "/" + fmt::fixed (now.stereo * 100.0, 0)
                       + " % (low cut/noise/stereo)";
            };
            auto* sel = typeSelectors (*editor)[0];
            const auto press = [&] (float fromRight, const char* what) {
                pointAt (*sel, { (float) sel->getWidth() - fromRight, 8.5f }, true);
                sel->mouseUp ({ juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.f, 0.f, 0.f, 0.f, 0.f, sel, sel,
                                juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false });
                play (proc, *editor, loop, fs, 0.5);
                pump (50);
                std::printf ("switched: %-14s preset %d, %s\n", what, juce::roundToInt (proc.param (ids::preset).convertFrom0to1 (proc.param (ids::preset).getValue())),
                             intensities().toRawUTF8());
            };
            press (26.5f, "< to Off:");
            press (10.5f, "> back:");
            press (10.5f, "> next:");
        }

        if (state == "selftest")
        {
            selfTest (loop, fs);
            continue;
        }

        if (state == "edit-bench")
        {
            PresetEditorView view (proc);
            view.setSize (PresetEditorView::kWidth, PresetEditorView::kHeight);
            view.tick();
            pump (50);
            std::printf ("preset editor paint (2x): %6.2f ms\n", timePaint (view, 20));
            std::printf ("its lanes alone:          %6.2f ms\n", timePaint (view.laneEditor(), 20));
            continue;
        }

        if (state.startsWith ("edit"))
        {
            PresetEditorView view (proc);
            const bool small = state == "edit-small";
            view.setSize (small ? PresetEditorView::kMinWidth : PresetEditorView::kWidth, small ? PresetEditorView::kMinHeight : PresetEditorView::kHeight);
            view.tick();
            pump (50);   // the lane headers come in on the message loop
            editorState (state, view);
            pump (50);
            const auto img = view.createComponentSnapshot (view.getLocalBounds(), true, scale);
            const auto file = outDir.getChildFile ("buildapp_" + state + ".png");
            file.deleteFile();
            juce::FileOutputStream os (file);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("%s -> %s\n", state.toRawUTF8(), file.getFullPathName().toRawUTF8());
            continue;
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
    return failures > 0 ? 1 : 0;
}
