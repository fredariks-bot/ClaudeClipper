#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

//==============================================================================
class ClipperProcessor : public juce::AudioProcessor
{
public:
    ClipperProcessor();
    ~ClipperProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HardEdge Clipper"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void refreshOversampling (int idx);
    void updateTargets (bool snap);

    std::atomic<float> *inParam, *ceilParam, *shapeParam, *kneeParam,
                       *osParam, *mixParam, *outParam, *deltaParam;

    std::vector<std::unique_ptr<juce::dsp::Oversampling<float>>> oversamplers; // 2x..16x
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> delay { 8192 };
    juce::AudioBuffer<float> unclipped;

    juce::SmoothedValue<float> preSm, postSm, mixSm;
    int currentOs = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipperProcessor)
};

//==============================================================================
namespace
{
    constexpr int maxOsStages = 4; // 2^4 = 16x

    enum Shape { Hard = 0, Soft, Cubic, Sine };

    template <typename F>
    void applyClip (juce::dsp::AudioBlock<float> b, F&& f)
    {
        for (size_t ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getChannelPointer (ch);
            for (size_t i = 0; i < b.getNumSamples(); ++i)
                d[i] = f (d[i]);
        }
    }

    // Signal ist hier auf Ceiling = 1.0 normiert
    void clipBlock (juce::dsp::AudioBlock<float> b, int shape, float knee)
    {
        switch (shape)
        {
            case Hard:
                applyClip (b, [] (float x) { return juce::jlimit (-1.0f, 1.0f, x); });
                break;

            case Soft: // linear bis t, danach tanh-Uebergang auf 1.0 (C1-stetig)
            {
                const float w = knee, t = 1.0f - knee;
                applyClip (b, [t, w] (float x)
                {
                    const float a = std::abs (x);
                    if (a <= t) return x;
                    return std::copysign (t + w * std::tanh ((a - t) / w), x);
                });
                break;
            }

            case Cubic:
                applyClip (b, [] (float x)
                {
                    const float a = std::abs (x);
                    if (a >= 1.0f) return std::copysign (1.0f, x);
                    return 1.5f * x - 0.5f * x * x * x;
                });
                break;

            case Sine:
                applyClip (b, [] (float x)
                {
                    if (std::abs (x) >= 1.0f) return std::copysign (1.0f, x);
                    return std::sin (juce::MathConstants<float>::halfPi * x);
                });
                break;
        }
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ClipperProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input",
        NormalisableRange<float> (-24.f, 24.f, 0.01f), 0.f, AudioParameterFloatAttributes().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "ceiling", 1 }, "Ceiling",
        NormalisableRange<float> (-24.f, 0.f, 0.01f), -0.3f, AudioParameterFloatAttributes().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "shape", 1 }, "Shape",
        StringArray { "Hard", "Soft", "Cubic", "Sine" }, 1));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "knee", 1 }, "Knee (Soft)",
        NormalisableRange<float> (1.f, 100.f, 0.1f), 40.f, AudioParameterFloatAttributes().withLabel ("%")));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 }, "Oversampling",
        StringArray { "Off", "2x", "4x", "8x", "16x" }, 2));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.f, 100.f, 0.1f), 100.f, AudioParameterFloatAttributes().withLabel ("%")));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.f, 12.f, 0.01f), 0.f, AudioParameterFloatAttributes().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "delta", 1 }, "Delta", false));

    return { p.begin(), p.end() };
}

ClipperProcessor::ClipperProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    inParam    = apvts.getRawParameterValue ("input");
    ceilParam  = apvts.getRawParameterValue ("ceiling");
    shapeParam = apvts.getRawParameterValue ("shape");
    kneeParam  = apvts.getRawParameterValue ("knee");
    osParam    = apvts.getRawParameterValue ("os");
    mixParam   = apvts.getRawParameterValue ("mix");
    outParam   = apvts.getRawParameterValue ("output");
    deltaParam = apvts.getRawParameterValue ("delta");
}

bool ClipperProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void ClipperProcessor::prepareToPlay (double sr, int bs)
{
    oversamplers.clear();
    for (int s = 1; s <= maxOsStages; ++s)
    {
        auto os = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) s,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
            true, true);
        os->initProcessing ((size_t) bs);
        oversamplers.push_back (std::move (os));
    }

    delay.prepare ({ sr, (juce::uint32) bs, 2 });
    unclipped.setSize (2, bs, false, false, true);

    preSm.reset (sr, 0.02);
    postSm.reset (sr, 0.02);
    mixSm.reset (sr, 0.02);
    updateTargets (true);

    refreshOversampling ((int) osParam->load());
}

void ClipperProcessor::refreshOversampling (int idx)
{
    currentOs = idx;
    float latency = 0.0f;

    if (idx > 0)
    {
        oversamplers[(size_t) idx - 1]->reset();
        latency = oversamplers[(size_t) idx - 1]->getLatencyInSamples();
    }

    delay.reset();
    delay.setDelay (latency);
    setLatencySamples ((int) std::round (latency));
}

void ClipperProcessor::updateTargets (bool snap)
{
    const float ceil = juce::Decibels::decibelsToGain (ceilParam->load());
    const float pre  = juce::Decibels::decibelsToGain (inParam->load()) / ceil;
    const float post = ceil * juce::Decibels::decibelsToGain (outParam->load());
    const float mix  = mixParam->load() * 0.01f;

    if (snap)
    {
        preSm.setCurrentAndTargetValue (pre);
        postSm.setCurrentAndTargetValue (post);
        mixSm.setCurrentAndTargetValue (mix);
    }
    else
    {
        preSm.setTargetValue (pre);
        postSm.setTargetValue (post);
        mixSm.setTargetValue (mix);
    }
}

void ClipperProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n     = juce::jmin (buffer.getNumSamples(), unclipped.getNumSamples());

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const int osIdx = (int) osParam->load();
    if (osIdx != currentOs)
        refreshOversampling (osIdx);

    updateTargets (false);

    const int   shape = (int) shapeParam->load();
    const float knee  = juce::jlimit (0.01f, 1.0f, kneeParam->load() * 0.01f);
    const bool  delta = deltaParam->load() > 0.5f;

    float* io[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    float* un[2] = { unclipped.getWritePointer (0), unclipped.getWritePointer (1) };

    // 1) Input-Gain (+ Ceiling-Normierung), ungeclippte Kopie latenzkompensiert merken
    for (int i = 0; i < n; ++i)
    {
        const float g = preSm.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = io[ch][i] * g;
            io[ch][i] = x;
            delay.pushSample (ch, x);
            un[ch][i] = delay.popSample (ch);
        }
    }

    // 2) Clipping (optional oversampled)
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);

    if (osIdx == 0)
    {
        clipBlock (block, shape, knee);
    }
    else
    {
        auto& os = *oversamplers[(size_t) osIdx - 1];
        auto up  = os.processSamplesUp (block);
        clipBlock (up, shape, knee);
        os.processSamplesDown (block);
    }

    // 3) Mix / Delta / Output
    for (int i = 0; i < n; ++i)
    {
        const float post = postSm.getNextValue();
        const float mix  = mixSm.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float wet = io[ch][i];
            const float dry = un[ch][i];
            io[ch][i] = (delta ? (wet - dry) : (dry + (wet - dry) * mix)) * post;
        }
    }
}

juce::AudioProcessorEditor* ClipperProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void ClipperProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ClipperProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClipperProcessor();
}
