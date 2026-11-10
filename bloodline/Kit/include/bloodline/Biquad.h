#pragma once
// BloodlineKit - plain C++ biquad (RBJ cookbook). No JUCE dependency, so the
// DSP can be unit-tested offline. State is double precision for stability at
// low cutoffs (K-weighting runs a 38 Hz high-pass).
#include <algorithm>
#include <cmath>

namespace bloodline {

struct Biquad {
    static constexpr double kPi = 3.14159265358979323846;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process(float x) {
        const double in = x;
        const double y = b0 * in + z1;
        z1 = b1 * in - a1 * y + z2;
        z2 = b2 * in - a2 * y;
        return static_cast<float>(y);
    }

    void setCoeffs(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
        a1 = na1 / na0; a2 = na2 / na0;
    }

    void lowpass(double fs, double f, double q = 0.70710678118) {
        f = std::min(f, 0.49 * fs);
        const double w = 2.0 * kPi * f / fs, cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        setCoeffs((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }
    void highpass(double fs, double f, double q = 0.70710678118) {
        f = std::min(f, 0.49 * fs);
        const double w = 2.0 * kPi * f / fs, cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        setCoeffs((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }
    // Constant 0 dB peak-gain band-pass.
    void bandpass(double fs, double f, double q) {
        f = std::min(f, 0.49 * fs);
        const double w = 2.0 * kPi * f / fs, cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        setCoeffs(alpha, 0, -alpha, 1 + alpha, -2 * cw, 1 - alpha);
    }
};

} // namespace bloodline
