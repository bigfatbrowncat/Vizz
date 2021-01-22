#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "RingBuffer.h"

//#define RING_BUFFER_READ_SIZE   4096
#define VIZ_POINTS  512

#define _STR_HELPER(x) #x
#define STR(x) _STR_HELPER(x)

class Vizz : public juce::Component,
             public juce::OpenGLRenderer,
             public juce::AsyncUpdater
{
public:
    Vizz (std::shared_ptr<RingBuffer<GLfloat>> ringBuffer)
            : readBuffer (2, ringBuffer->getBufferSize()) //, forwardFFT (fftOrder)
    {
        // Sets the OpenGL version to 3.2
        openGLContext.setOpenGLVersionRequired (juce::OpenGLContext::OpenGLVersion::openGL3_2);
        
        this->ringBuffer = ringBuffer;

        // Allocate FFT data
        //fftData = new GLfloat [2 * fftSize];

        // Attach the OpenGL context but do not start [ see start() ]
        openGLContext.setRenderer(this);
        openGLContext.attachTo(*this);
        
        // Setup a pixel format object to tell the context what level of
        // multisampling to use.
        juce::OpenGLPixelFormat pixelFormat;
        pixelFormat.multisamplingLevel = 4; // Change this value to your needs.
        
        openGLContext.setPixelFormat(pixelFormat);
        
        // Setup GUI Overlay Label: Status of Shaders, compiler errors, etc.
        //addAndMakeVisible (statusLabel);
        //statusLabel.setJustificationType (juce::Justification::topLeft);
        //statusLabel.setFont (juce::Font (14.0f));
        
    }
    
    ~Vizz()
    {
        
        shader = nullptr;
        uniforms = nullptr;
      
        // Turn off OpenGL
        openGLContext.setContinuousRepainting (false);
        openGLContext.detach();
        
        //delete [] fftData;

        // Detach ringBuffer
        ringBuffer = nullptr;
    }
    
    void handleAsyncUpdate() override
    {
        //statusLabel.setText (statusText, juce::dontSendNotification);
    }
    

    // Control Functions

    void start()
    {
        openGLContext.setContinuousRepainting (true);
    }
  
    void stop()
    {
        openGLContext.setContinuousRepainting (false);
    }
    
    
    //==========================================================================
    // OpenGL Callbacks
    
    /** Called before rendering OpenGL, after an OpenGLContext has been associated
        with this OpenGLRenderer (this component is a OpenGLRenderer).
        Sets up GL objects that are needed for rendering.
     */
    void newOpenGLContextCreated() override
    {
        // Setup Shaders
        createShaders();
        
        // Setup Buffer Objects
        openGLContext.extensions.glGenBuffers (1, &VBO); // Vertex Buffer Object
        openGLContext.extensions.glGenBuffers (1, &EBO); // Element Buffer Object
    }
    
    /** Called when done rendering OpenGL, as an OpenGLContext object is closing.
        Frees any GL objects created during rendering.
     */
    void openGLContextClosing() override
    {
        shader.release();
        uniforms.release();
    }
    
    // Normalized to the kernel_size
    void convolution(float* arr, size_t arr_size, float* kernel, size_t kernel_size, std::vector<float>& res) {
        if (res.size() != arr_size - kernel_size + 1) {
            res = std::vector<float>(arr_size - kernel_size + 1, 0.0f);
        }
        
        for (size_t start = 0; start < res.size(); start++) {
            // Integrating
            double sum = 0.0;
            for (size_t i = 0; i < kernel_size; i++) {
                sum += arr[i + start] * kernel[i];
            }
            res[start] = sum;
        }
    }
    
    /** The OpenGL rendering callback.
     */
    void renderOpenGL() override
    {
        jassert (juce::OpenGLHelpers::isContextActive());
        
        // Setup Viewport
        const float renderingScale = (float) openGLContext.getRenderingScale();
        glViewport (0, 0,
                    juce::roundToInt (renderingScale * getWidth()),
                    juce::roundToInt (renderingScale * getHeight())
        );
        
        // Set background Color
        juce::OpenGLHelpers::clear (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
        
        // Enable Alpha Blending
        glEnable (GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        // Use Shader Program that's been defined
        shader->use();
        
        // Setup the Uniforms for use in the Shader
        
        if (uniforms->resolution != nullptr)
            uniforms->resolution->set ((GLfloat) renderingScale * getWidth(), (GLfloat) renderingScale * getHeight());

        // Read in samples from ring buffer
        if (uniforms->audioSampleData != nullptr && ringBuffer != nullptr)
        {
            int K = 2;

            if (current.size() != ringBuffer->getBufferSize() / K) {
                current = std::vector<float>(ringBuffer->getBufferSize() / K, 0.0);
            } else {
                std::fill(current.begin(), current.end(), 0.0);
            }
            
            ringBuffer->readSamples (readBuffer, ringBuffer->getBufferSize());
            
            // Copying the data
            for (int smp = 0; smp < ringBuffer->getBufferSize(); smp++) {
                current[smp / K] += (*(float*)readBuffer.getReadPointer(0, smp) + *(float*)readBuffer.getReadPointer(1, smp)) / (2.0 * K);
            }
            
            // Finding the correlation between current buffer and the former visualizationBuffer
            convolution(&current[0], current.size(), visualizationBuffer, VIZ_POINTS, correlation);
            
            float corr_max = correlation[0];
            size_t sync_pos = 0;
            for (int i = 0; i < correlation.size(); i++) {
                if (correlation[i] > corr_max) {
                    corr_max = correlation[i];
                    sync_pos = i;
                }
            }
            
            //std::cout << "corr_max: " << corr_max << ", sync_pos: " << sync_pos << std::endl;
            for (int i = 0; i < VIZ_POINTS; i++) {
                visualizationBuffer[i] = current[i + sync_pos];
            }
            

//            // Doing the FFT analyis
//            //juce::FloatVectorOperations::clear (fftData, fftSize);
//
//            //std::cout << "0: " << fftData[0] << std::endl;
//            // Putting the samples into FFT buffer
//            //for (int i = 0; i < 2; ++i)
//            //{
//            //    juce::FloatVectorOperations::add (fftData, readBuffer.getReadPointer(i, 0), RING_BUFFER_READ_SIZE);
//            //}
//            //std::cout << "1: " << fftData[0] << std::endl;
//
//            // Doing forward FFT transformation on the buffer
//            //forwardFFT.performFrequencyOnlyForwardTransform (fftData);
//
//            std::array<std::complex<float>, fftSize> input, output;
//            std::fill(std::begin(input), std::end(input), 0.0);
//            std::fill(std::begin(output), std::end(output), 0.0);
//            // Putting the samples into FFT buffer
//            for (int ch = 0; ch < 2; ++ch)
//            {
//                for (int smp = 0; smp < RING_BUFFER_READ_SIZE; smp++) {
//                    input[smp] += *(float*)readBuffer.getReadPointer(ch, smp);
//                }
//            }
//            forwardFFT.perform (&input[0], &output[0], false);
//
//            static float base_freq = 23;
//            static float old_base_freq1 = 23;
//            static float old_base_freq2 = 23;
//            static float old_base_freq3 = 23;
//            static float old_base_freq4 = 23;
//
//            old_base_freq4 = old_base_freq3;
//            old_base_freq3 = old_base_freq2;
//            old_base_freq2 = old_base_freq1;
//            old_base_freq1 = base_freq;
//
//
//            float base_freq1 = 0;
//            float base_freq2 = 0;
//            float base_freq3 = 0;
//
//            // Finding the highest freq
//            std::complex<float> freq_value1 = input[0];
//            std::complex<float> freq_value2 = input[0];
//            std::complex<float> freq_value3 = input[0];
//            for (int i = 22; i < 43 /*fftSize / 8*/; i++) {
//                if (output[i].real() > freq_value1.real()) {
//                    freq_value1 = output[i];
//                    base_freq1 = i + 1;
//                } else if (output[i].real() > freq_value2.real()) {
//                    freq_value2 = output[i];
//                    base_freq2 = i + 1;
//                } else if (output[i].real() > freq_value3.real()) {
//                    freq_value3 = output[i];
//                    base_freq3 = i + 1;
//                }
//            }
//
////            if (freq_value2.real() / freq_value1.real() > 0.75) {
////                base_freq = 2.0 / ( 1.0 / base_freq1 + 1.0 / base_freq2 /*+ 1.0 / base_freq3*/ );
////            } else {
//
//            if (base_freq1 >= 22 - 1)
//                base_freq = base_freq1;
////            }
//
//            std::cout << "base: " << base_freq1 << ", " << base_freq2 << ", " << base_freq3 << " <- " << base_freq << std::endl;
//
//
//            // Prevent synchronization shivering
//            //if (abs(base_freq_index - old_base_freq_index) == 1) {
//            //    base_freq_index = old_base_freq_index;
//            //}
//
//            /*base_freq = 1.0 / 64 * base_freq +
//                        7.0 / 64 * old_base_freq1 +
//                        8.0 / 64 * old_base_freq2 +
//                        16.0 / 64 * old_base_freq3 +
//                        32.0 / 64 * old_base_freq4;*/
//
//
//            // Setting all other frequencies to 0 for backward FFT
//            std::fill(std::begin(input), std::end(input), 0.0);
//            for (int i = 0; i <= base_freq; i++) {
//                input[i] = output[i];// * (i / base_freq);
//            }
//
//            // Backward FFT
//            forwardFFT.perform (&input[0], &output[0], true);
//
//
//            std::vector<int> zero_cross_indices;
//
//
//            // Searching for the first zero on the base frequency
//            float x = output[0].real();
//            for (int i = 0; i < RING_BUFFER_READ_SIZE; i++) {
//                float xnew = output[i].real();
//                if (xnew >= 0 && x < 0) {
//                    zero_cross_indices.push_back(i);
//                    break;
//                }
//                x = xnew;
//            }
//
//            // If wh have found only one zero crossing, adding a fake one inn the beginning
//            //while (zero_cross_indices.size() < 2) zero_cross_indices.insert(zero_cross_indices.begin(), 0);
//
//            if (zero_cross_indices.size() > 0) {
//                zero_cross_indices.push_back(zero_cross_indices[0] + 1024);
//
//                std::cout << zero_cross_indices[0] << ", " << zero_cross_indices.back() << std::endl;
//
//                // If we have found too many, let's cut some off
//                if (zero_cross_indices.size() > 3) {
//                    zero_cross_indices = std::vector<int>(zero_cross_indices.begin(), zero_cross_indices.begin() + 3);
//                }
//
//                for (int i = 0; i < VIZ_POINTS; i++) {
//                    visualizationBuffer[i] = 0;
//                }
//
//                bool set_successfully = true;
//                for (int i = 0; i < VIZ_POINTS; i++) {
//                    int index = zero_cross_indices[0] + i * (zero_cross_indices.back() - zero_cross_indices[0]) / VIZ_POINTS;
//                    //if (index >= RING_BUFFER_READ_SIZE) set_successfully = false;
//                    visualizationBuffer[i] = (*(float*)readBuffer.getReadPointer(0, index % RING_BUFFER_READ_SIZE));
//                }
//                /*
//                for (int i = 0; i < RING_BUFFER_READ_SIZE; i++) {
//                    //int index = (int)(8 * index_wrapped / (base_freq_index + 1));
//                    //if (i < VIZ_POINTS && index < RING_BUFFER_READ_SIZE) {
//
//                    int index = i + zero_cross_index;
//
//                    if (i < VIZ_POINTS) {
//                        visualizationBuffer[i] = (*(float*)readBuffer.getReadPointer(0, index % RING_BUFFER_READ_SIZE));
//                    }
//
//                    //}
//                }*/
//
//    //            // Sum channels together
//    //            for (int i = 0; i < 2; ++i)
//    //            {
//    //                juce::FloatVectorOperations::add (visualizationBuffer, readBuffer.getReadPointer(i, 0), RING_BUFFER_READ_SIZE);
//    //            }
//
//
//                if (set_successfully)
            
            
            
            //}
        }
        else {
            juce::FloatVectorOperations::clear (visualizationBuffer, VIZ_POINTS);
        }
        uniforms->audioSampleData->set(visualizationBuffer, ringBuffer->getBufferSize());

        // Define Vertices for a Square (the view plane)
        GLfloat vertices[] = {
            1.0f,   1.0f,  0.0f,  // Top Right
            1.0f,  -1.0f,  0.0f,  // Bottom Right
            -1.0f, -1.0f,  0.0f,  // Bottom Left
            -1.0f,  1.0f,  0.0f   // Top Left
        };
        // Define Which Vertex Indexes Make the Square
        GLuint indices[] = {  // Note that we start from 0!
            0, 1, 3,   // First Triangle
            1, 2, 3    // Second Triangle
        };
        
        // Vertex Array Object stuff for later
        //openGLContext.extensions.glGenVertexArrays(1, &VAO);
        //openGLContext.extensions.glBindVertexArray(VAO);
        
        // VBO (Vertex Buffer Object) - Bind and Write to Buffer
        openGLContext.extensions.glBindBuffer (GL_ARRAY_BUFFER, VBO);
        openGLContext.extensions.glBufferData (GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
                                                                    // GL_DYNAMIC_DRAW or GL_STREAM_DRAW
                                                                    // Don't we want GL_DYNAMIC_DRAW since this
                                                                    // vertex data will be changing alot??
                                                                    // test this
        
        // EBO (Element Buffer Object) - Bind and Write to Buffer
        openGLContext.extensions.glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, EBO);
        openGLContext.extensions.glBufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);
                                                                    // GL_DYNAMIC_DRAW or GL_STREAM_DRAW
                                                                    // Don't we want GL_DYNAMIC_DRAW since this
                                                                    // vertex data will be changing alot??
                                                                    // test this
        
        // Setup Vertex Attributes
        openGLContext.extensions.glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);
        openGLContext.extensions.glEnableVertexAttribArray (0);
    
        // Draw Vertices
        //glDrawArrays (GL_TRIANGLES, 0, 6); // For just VBO's (Vertex Buffer Objects)
        glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0); // For EBO's (Element Buffer Objects) (Indices)
        
    
        
        // Reset the element buffers so child Components draw correctly
        openGLContext.extensions.glBindBuffer (GL_ARRAY_BUFFER, 0);
        openGLContext.extensions.glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
        //openGLContext.extensions.glBindVertexArray(0);
    }
    
    
    //==========================================================================
    // JUCE Callbacks
    
    void paint (juce::Graphics& g) override
    {
    }
    
    void resized () override
    {
        //statusLabel.setBounds (getLocalBounds().reduced (4).removeFromTop (75));
    }
    
private:
    
    //==========================================================================
    // OpenGL Functions
    
    
    /** Loads the OpenGL Shaders and sets up the whole ShaderProgram
    */
    void createShaders()
    {
        vertexShader =
        "attribute vec3 position;\n"
        "\n"
        "void main()\n"
        "{\n"
        "    gl_Position = vec4(position, 1.0);\n"
        "}\n";
        
        fragmentShader =
        "uniform vec2  resolution;\n"
        "uniform float audioSampleData[" STR(VIZ_POINTS) "];\n"
        "\n"
        "void getAmplitudeForXPos (in float xPos, out float audioAmplitude)\n"
        "{\n"
        // Buffer size - 1
        "   float perfectSamplePosition = 255.0 * xPos / resolution.x;\n"
        "   int leftSampleIndex = int (floor (perfectSamplePosition));\n"
        "   int rightSampleIndex = int (ceil (perfectSamplePosition));\n"
        "   audioAmplitude = mix (audioSampleData[leftSampleIndex], audioSampleData[rightSampleIndex], fract (perfectSamplePosition));\n"
        "}\n"
        "\n"
        "#define THICKNESS 0.01\n"
        "void main()\n"
        "{\n"
        "    float y = gl_FragCoord.y / resolution.y;\n"
        "    float amplitude = 0.0;\n"
        "    getAmplitudeForXPos (gl_FragCoord.x, amplitude);\n"
        "\n"
        // Centers & Reduces Wave Amplitude
        "    amplitude = 0.5 - amplitude;\n"
        "    float intensity = abs (THICKNESS / (amplitude - y)) + 0.25;\n"
        "    float r = intensity * intensity; \n"
        "    float g = -1.5 * intensity * max(0, (y - 0.5) * (y - 0.5)) + 0.90 * intensity;\n"
        "    float b = 0.7 * intensity * intensity + 0.10; \n"
        "\n"
        "    gl_FragColor = vec4 (r, g, b, 1.0);\n"
        "}\n";
        
        std::unique_ptr<juce::OpenGLShaderProgram> shaderProgramAttempt = std::make_unique<juce::OpenGLShaderProgram> (openGLContext);
        
        // Sets up pipeline of shaders and compiles the program
        if (shaderProgramAttempt->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (vertexShader))
            && shaderProgramAttempt->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (fragmentShader))
            && shaderProgramAttempt->link())
        {
            uniforms.release();
            shader = std::move (shaderProgramAttempt);
            uniforms.reset (new Uniforms (openGLContext, *shader));
            
            //statusText = "GLSL: v" + juce::String (juce::OpenGLShaderProgram::getLanguageVersion(), 2);
        }
        else
        {
            //statusText = shaderProgramAttempt->getLastError();
        }
        
        triggerAsyncUpdate();
    }
    

    //==============================================================================
    // This class just manages the uniform values that the fragment shader uses.
    struct Uniforms
    {
        Uniforms (juce::OpenGLContext& openGLContext, juce::OpenGLShaderProgram& shaderProgram)
        {
            //projectionMatrix = createUniform (openGLContext, shaderProgram, "projectionMatrix");
            //viewMatrix       = createUniform (openGLContext, shaderProgram, "viewMatrix");
            
            resolution.reset (createUniform (openGLContext, shaderProgram, "resolution"));
            audioSampleData.reset (createUniform (openGLContext, shaderProgram, "audioSampleData"));
            
        }
        
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> resolution, audioSampleData;
        
    private:
        static juce::OpenGLShaderProgram::Uniform* createUniform (juce::OpenGLContext& openGLContext,
                                                            juce::OpenGLShaderProgram& shaderProgram,
                                                            const char* uniformName)
        {
            if (openGLContext.extensions.glGetUniformLocation (shaderProgram.getProgramID(), uniformName) < 0)
                return nullptr;
            
            return new juce::OpenGLShaderProgram::Uniform (shaderProgram, uniformName);
        }
    };
  
    // OpenGL Variables
    juce::OpenGLContext openGLContext;
    GLuint VBO, VAO, EBO;
    
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    std::unique_ptr<Uniforms> uniforms;
    
    const char* vertexShader;
    const char* fragmentShader;

    // Audio Buffer
    std::shared_ptr<RingBuffer<GLfloat>> ringBuffer;
    juce::AudioBuffer<GLfloat> readBuffer;    // Stores data read from ring buffer
    GLfloat visualizationBuffer [VIZ_POINTS];    // Single channel to visualize
  
    // Overlay GUI
    /*juce::String statusText;
    juce::Label statusLabel;*/
  
    // FFT
    //juce::dsp::FFT forwardFFT;
    //GLfloat * fftData;
    //size_t sample_index[1];
                
    std::vector<float> current;
    std::vector<float> correlation;

    // This is so that we can initialize fowardFFT in the constructor with the order
    /*enum
    {
        fftOrder = 14,
        fftSize  = 1 << fftOrder // set 11th bit to one
    };*/
    
    
    /** DEV NOTE
        If I wanted to optionally have an interchangeable shader system,
        this would be fairly easy to add. Chack JUCE Demo -> OpenGLDemo.cpp for
        an implementation example of this. For now, we'll just allow these
        shader files to be static instead of interchangeable and dynamic.
        String newVertexShader, newFragmentShader;
     */
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Vizz)
};
