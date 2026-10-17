// BuildApp without a DAW: loops a built-in psytrance loop (or your file)
// through the plugin and out of the speakers, with the editor below a small
// transport bar.
//   buildapp_demo [file.wav]
//
// BYPASS plays the loop untouched for comparison. Space plays and stops.

#include "TestUtil.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

using namespace theme;

namespace
{
    struct Material
    {
        juce::String name;
        double rate = 0.0;
        juce::AudioBuffer<float> audio;   // stereo
    };

    std::shared_ptr<Material> makeLoop (double fs)
    {
        auto m = std::make_shared<Material>();
        m->rate = fs;
        m->name = "Built-in loop, 145 BPM";
        const auto loop = testutil::psyLoop (fs, 60.0 / 145.0 * 16.0);   // four bars
        m->audio.setSize (2, (int) loop.size());
        m->audio.copyFrom (0, 0, loop.left.data(), (int) loop.size());
        m->audio.copyFrom (1, 0, loop.right.data(), (int) loop.size());
        return m;
    }

    // Reads a file as stereo at `fs`.
    std::shared_ptr<Material> makeFromFile (const juce::File& f, double fs)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples <= 0) return nullptr;
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 600.0));
        juce::AudioBuffer<float> raw ((int) std::max (1u, std::min (2u, r->numChannels)), n);
        r->read (&raw, 0, n, 0, true, raw.getNumChannels() > 1);
        const double ratio = r->sampleRate / fs;
        const int m = (int) std::floor (n / ratio);
        auto mat = std::make_shared<Material>();
        mat->rate = fs;
        mat->name = f.getFileName();
        mat->audio.setSize (2, m);
        for (int c = 0; c < 2; ++c)
        {
            juce::LagrangeInterpolator li;
            li.process (ratio, raw.getReadPointer (std::min (c, raw.getNumChannels() - 1)), mat->audio.getWritePointer (c), m);
        }
        return mat;
    }
} // namespace

// Feeds the processor from the device callback, like a host would.
class DemoAudio : public juce::AudioIODeviceCallback
{
public:
    explicit DemoAudio (BuildAppProcessor& p) : proc (p) {}

    std::function<void (double)> onRate;   // message thread: the device runs at a new rate
    std::atomic<bool> playing { true }, bypass { false };

    void setMaterial (std::shared_ptr<Material> m)
    {
        const juce::SpinLock::ScopedLockType l (lock);
        material = std::move (m);
        pos = 0;
    }

    double rate() const noexcept { return fs.load(); }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        const double rate = d->getCurrentSampleRate();
        const int block = std::max (16, d->getCurrentBufferSizeSamples());
        proc.setRateAndBufferSizeDetails (rate, block);
        proc.prepareToPlay (rate, block);
        capacity = block;
        work.setSize (2, block);
        fs = rate;
        juce::MessageManager::callAsync ([this, rate] { if (onRate) onRate (rate); });
    }

    void audioDeviceStopped() override { proc.releaseResources(); }

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

        const juce::SpinLock::ScopedTryLockType l (lock);
        if (! l.isLocked() || material == nullptr || ! playing || std::abs (material->rate - fs.load()) > 0.5) return;

        const auto& m = material->audio;
        const int len = m.getNumSamples();
        juce::MidiBuffer midi;
        for (int done = 0; done < n;)
        {
            const int chunk = std::min ({ n - done, capacity, len - (int) pos });
            work.setSize (2, chunk, false, false, true);
            for (int c = 0; c < 2; ++c)
                work.copyFrom (c, 0, m, c, (int) pos, chunk);
            proc.processBlock (work, midi);
            for (int c = 0; c < std::min (numOut, 2); ++c)
                if (out[c] != nullptr)
                    juce::FloatVectorOperations::copy (out[c] + done, bypass ? m.getReadPointer (c, (int) pos) : work.getReadPointer (c), chunk);
            pos = (pos + chunk) % len;
            done += chunk;
        }
    }

private:
    BuildAppProcessor& proc;
    juce::SpinLock lock;
    std::shared_ptr<Material> material;
    juce::int64 pos = 0;
    std::atomic<double> fs { 0.0 };
    juce::AudioBuffer<float> work;
    int capacity = 0;
};

// Night-sheet buttons for the transport bar.
class BarLookAndFeel : public NightLookAndFeel
{
public:
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return label (10.f); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f).translated (0.f, down ? 1.f : 0.f);
        if (b.getToggleState())
        {
            g.setColour (col::ink);
            g.fillRoundedRectangle (r, kRadius);
        }
        else
        {
            g.setColour (over ? col::ink : col::inkSoft);
            g.drawRoundedRectangle (r, kRadius, 1.f);
        }
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        g.setFont (label (10.f));
        g.setColour (b.getToggleState() ? col::sheet : col::ink);
        g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds().translated (0, down ? 1 : 0), juce::Justification::centred, false);
    }
};

class DemoComponent : public juce::Component, private juce::ComponentListener
{
public:
    static constexpr int kBar = 52;

    explicit DemoComponent (juce::File f) : audio (proc), file (std::move (f))
    {
        setLookAndFeel (&lnf);
        setOpaque (true);
        editor.reset (dynamic_cast<BuildAppEditor*> (proc.createEditor()));
        addAndMakeVisible (*editor);

        // Space plays and stops from anywhere, so the buttons don't take the focus.
        for (auto* b : { &play, &bypass, &source })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
        play.setClickingTogglesState (true);
        play.setToggleState (true, juce::dontSendNotification);
        play.setButtonText ("Stop");
        play.onClick = [this] { audio.playing = play.getToggleState(); play.setButtonText (play.getToggleState() ? "Stop" : "Play"); };
        bypass.setButtonText ("Bypass");
        bypass.setClickingTogglesState (true);
        bypass.onClick = [this] { audio.bypass = bypass.getToggleState(); };
        source.setButtonText (juce::String (juce::CharPointer_UTF8 ("Open file\xe2\x80\xa6")));
        source.onClick = [this] { chooseFile(); };

        audio.onRate = [this] (double) { reload(); };
        const auto err = devices.initialiseWithDefaultDevices (0, 2);
        if (err.isNotEmpty()) note = "Audio device: " + err;
        devices.addAudioCallback (&audio);

        setWantsKeyboardFocus (true);
        // The editor changes its own height (the build band folds away with
        // no preset): the window follows it.
        editor->addComponentListener (this);
        setSize (editor->getWidth(), editor->getHeight() + kBar);
    }

    ~DemoComponent() override
    {
        devices.removeAudioCallback (&audio);
        editor->removeComponentListener (this);
        editor.reset();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::sheet);
        auto bar = getLocalBounds().removeFromTop (kBar).toFloat();
        g.setColour (col::hairline);
        g.fillRect (bar.removeFromBottom (1.f));
        caption (g, "Demo host", { 24.f, 6.f, 100.f, 12.f }, juce::Justification::centredLeft, col::accent, 9.f);
        g.setFont (mono (10.f));
        g.setColour (col::inkSoft);
        g.drawText (note, juce::Rectangle<float> (360.f, 8.f, (float) getWidth() - 384.f, 36.f), juce::Justification::centredRight, true);
    }

    void resized() override
    {
        play.setBounds (24, 20, 64, 24);
        bypass.setBounds (96, 20, 74, 24);
        source.setBounds (178, 20, 96, 24);
        editor->setTopLeftPosition (0, kBar);
    }

    void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
    {
        if (&c == editor.get() && wasResized) setSize (editor->getWidth(), editor->getHeight() + kBar);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey) { play.triggerClick(); return true; }
        return false;
    }

    void visibilityChanged() override
    {
        if (isShowing()) grabKeyboardFocus();
    }

private:
    // (Re)builds the material at the device's rate.
    void reload()
    {
        const double fs = audio.rate();
        if (fs <= 0.0) return;
        std::shared_ptr<Material> m;
        if (file.existsAsFile()) m = makeFromFile (file, fs);
        if (m == nullptr) m = makeLoop (fs);
        note = m->name;
        audio.setMaterial (std::move (m));
        repaint (0, 0, getWidth(), kBar);
    }

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Audio to play through BuildApp", file, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& c) {
            if (c.getResult().existsAsFile())
            {
                file = c.getResult();
                reload();
            }
        });
    }

    BuildAppProcessor proc;
    DemoAudio audio;
    juce::AudioDeviceManager devices;
    BarLookAndFeel lnf;
    std::unique_ptr<BuildAppEditor> editor;
    juce::TextButton play, bypass, source;
    juce::File file;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String note { "Starting audio..." };
};

class DemoApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "BuildApp Demo"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }

    void initialise (const juce::String& args) override
    {
        const auto parts = juce::StringArray::fromTokens (args, true);
        const auto file = parts.isEmpty() ? juce::File() : juce::File::getCurrentWorkingDirectory().getChildFile (parts[0].unquoted());
        window = std::make_unique<Window> (std::make_unique<DemoComponent> (file));
    }

    void shutdown() override { window.reset(); }

private:
    struct Window : juce::DocumentWindow
    {
        explicit Window (std::unique_ptr<DemoComponent> content)
            : DocumentWindow ("BuildApp Demo", col::sheet, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content.release(), true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::quit(); }
    };

    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (DemoApp)
