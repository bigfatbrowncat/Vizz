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
            : readBuffer (2, ringBuffer->getBufferSize()), forwardFFT (fftOrder)
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
        
        juce::FloatVectorOperations::clear (visualizationBuffer, VIZ_POINTS);

        warmth = 0.0f;
        cool = 0.0f;
    }
    
    ~Vizz()
    {
        
        shader.release();
        uniforms.release();
      
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
            if (zoom < 1) zoom = 1;
            if (zoom > 4) zoom = 4;
            float K = zoom;
          
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
            
            // Using FFT to measure warmth
            std::vector<std::complex<float>> input(fftSize), output(fftSize);
            std::fill(std::begin(input), std::end(input), 0.0);
            std::fill(std::begin(output), std::end(output), 0.0);
            
            // Putting the samples into FFT buffer
            for (int smp = 0; smp < fftSize; smp++) {
                input[smp] += visualizationBuffer[smp];
            }
            forwardFFT.perform (&input[0], &output[0], false);
            
            // Searching for the max harmonic amplitude among the lowest ones
            float max_harm = 0.0f;
            
            float avg_harm_warmth = 0.0f;
            float avg_harm_warmth_norm = 0.0f;
            float avg_harm_cool = 0.0f;
            float avg_harm_cool_norm = 0.0f;

            // Warmth should reflect low frequencies
            for (int i = 0; i < 4; i++) {
                if (std::abs(output[i].real()) > max_harm) {
                    max_harm = std::abs(output[i].real());
                }
                avg_harm_warmth += std::abs(output[i].real()) / (i + 2);
                avg_harm_warmth_norm += 12.0 / (i + 3);
            }
            avg_harm_warmth /= avg_harm_warmth_norm;
            if (warmth < avg_harm_warmth) warmth = avg_harm_warmth;
            if (warmth > 1.0) warmth = 1.0;
            warmth *= 0.99;

            // Cool should reflex high frequencies
            for (int i = 20; i < fftSize / 2; i++) {
                if (std::abs(output[i].real()) > max_harm) {
                    max_harm = std::abs(output[i].real());
                }
                avg_harm_cool += 2.0 * std::abs(output[i].real()) * i;
                avg_harm_cool_norm += i;
            }
            avg_harm_cool /= avg_harm_cool_norm;
            if (cool < avg_harm_cool) cool = avg_harm_cool;
            if (cool > 1.0) cool = 1.0;
            cool *= 0.99;

            /*int band = fftSize / 64;
             if (max_harm_index < band) {
             warmth = 1.0;
             } else {
             warmth = band / max_harm_index;
             }*/
            
            std::cout << "warmth: " << warmth << ", cool: " << cool << std::endl;
            

        }
        
        uniforms->warmth->set((GLfloat)warmth);
        uniforms->cool->set((GLfloat)cool);
        uniforms->audioSampleData->set(visualizationBuffer, VIZ_POINTS);

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
    
    void setZoom(int zoom)
    {
        this->zoom = zoom;
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
        "uniform float warmth;\n"
        "uniform float cool;\n"
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
        "    float g = -1.5 * intensity * max(0, (y - 0.5) * (y - 0.5)) + 0.85 * intensity + 0.1 * warmth * warmth;\n"
        "    float r = intensity * intensity + 1.5 * warmth * warmth * g * (1 - y) * (1 - y); \n"
        "    float b = 0.7 * intensity * intensity + 0.10 + 2.5 * cool * cool * g * y * y; \n"
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
            
            cool.reset (createUniform (openGLContext, shaderProgram, "cool"));
            warmth.reset (createUniform (openGLContext, shaderProgram, "warmth"));
            resolution.reset (createUniform (openGLContext, shaderProgram, "resolution"));
            audioSampleData.reset (createUniform (openGLContext, shaderProgram, "audioSampleData"));
            
        }
        
        ~Uniforms() {
            cool = nullptr;
            warmth = nullptr;
            resolution = nullptr;
            audioSampleData = nullptr;
        }
        
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> resolution, warmth, cool, audioSampleData;
        
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
    
    float warmth, cool;
    
    const char* vertexShader;
    const char* fragmentShader;

    enum
    {
        fftOrder = 9,
        fftSize  = 1 << fftOrder // set 10th bit to one
    };
    
    // Audio Buffer
    std::shared_ptr<RingBuffer<GLfloat>> ringBuffer;
    juce::AudioBuffer<GLfloat> readBuffer;    // Stores data read from ring buffer
    GLfloat visualizationBuffer [VIZ_POINTS];    // Single channel to visualize
  
    int zoom;
    
    // Overlay GUI
    /*juce::String statusText;
    juce::Label statusLabel;*/
  
    // FFT
    juce::dsp::FFT forwardFFT;
    //GLfloat * fftData;
    //size_t sample_index[1];
                
    std::vector<float> current;
    std::vector<float> correlation;

    // This is so that we can initialize fowardFFT in the constructor with the order
    
   
    /** DEV NOTE
        If I wanted to optionally have an interchangeable shader system,
        this would be fairly easy to add. Chack JUCE Demo -> OpenGLDemo.cpp for
        an implementation example of this. For now, we'll just allow these
        shader files to be static instead of interchangeable and dynamic.
        String newVertexShader, newFragmentShader;
     */
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Vizz)
};
