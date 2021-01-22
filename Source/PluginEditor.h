#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Vizz.h"

class VizzAudioProcessorEditor  : public juce::AudioProcessorEditor, juce::ChangeListener
{
public:
    VizzAudioProcessorEditor (VizzAudioProcessor&);
    ~VizzAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    std::shared_ptr<RingBuffer<GLfloat>> getRingBuffer() { return ringBuffer; }
  
private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    VizzAudioProcessor& audioProcessor;
  
    std::shared_ptr<RingBuffer<GLfloat>> ringBuffer;
    Vizz scope2d;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VizzAudioProcessorEditor)
};


