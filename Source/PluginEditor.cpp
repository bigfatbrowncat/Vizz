#include "PluginProcessor.h"
#include "PluginEditor.h"

VizzAudioProcessorEditor::VizzAudioProcessorEditor (VizzAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), //mTextChangesListener(this),
      ringBuffer(std::make_shared<RingBuffer<GLfloat>>(2, 2048 + 1024)), scope2d(ringBuffer)

{
    addAndMakeVisible(scope2d);
  
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (600, 300);
  
    p.addChangeListener (this);
    p.setRingBuffer(ringBuffer);
  
    // This doesn't work for AU
    setResizeLimits (150, 300, 900, 300);
    setResizable (true, true);
  
    scope2d.start();
}

VizzAudioProcessorEditor::~VizzAudioProcessorEditor()
{
    scope2d.stop();

    VizzAudioProcessor& npap = dynamic_cast<VizzAudioProcessor&>(processor);
    npap.removeChangeListener (this);
}

void VizzAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void VizzAudioProcessorEditor::resized()
{
    scope2d.setBounds(0, 0, getWidth(), getHeight());
}

void VizzAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    scope2d.setZoom(audioProcessor.zoom->get());
    repaint();
}
