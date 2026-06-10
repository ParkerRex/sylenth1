#pragma once

#include <array>

namespace synth
{
enum class LfoShape
{
    Sine,
    Triangle,
    SawUp,
    SawDown,
    Square,
    SampleHold,
    Step
};

inline constexpr int lfoMaxSteps = 16;

class Lfo
{
public:
    void prepare(double newSampleRate) noexcept;
    void setRateHz(float newRateHz) noexcept;
    void setShape(LfoShape newShape) noexcept;
    void setPhaseDegrees(float degrees) noexcept;
    // Step-sequencer table for LfoShape::Step. One LFO cycle spans the active
    // steps; smooth crossfades the tail of each step into the next. Editing
    // step values never resets phase, so live tweaks stay click-free.
    void setSteps(const float* stepValues, int count, float smooth) noexcept;
    void resetPhase() noexcept;
    float process() noexcept;

    float getValue() const noexcept { return value; }
    float getPhase() const noexcept { return phase; }

private:
    float valueForPhase(float phaseValue) noexcept;
    void updatePhaseIncrement() noexcept;

    double sampleRate = 44100.0;
    float rateHz = 2.0f;
    float phaseIncrement = 2.0f / 44100.0f;
    float phase = 0.0f;
    float phaseOffset = 0.0f;
    float value = 0.0f;
    float heldRandom = 0.0f;
    unsigned int randomState = 0x12345678u;
    LfoShape shape = LfoShape::Sine;
    std::array<float, lfoMaxSteps> steps {};
    int stepCount = 8;
    float stepSmooth = 0.0f;
};
} // namespace synth
