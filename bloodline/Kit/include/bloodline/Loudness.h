#pragma once
// BloodlineKit - ITU-R BS.1770 K-weighting (pre-filter shelf + RLB high-pass),
// designed for any sample rate. Used by GHOST's Match and by the test runner.
#include "Biquad.h"

namespace bloodline {

struct KWeighting {
    Biquad shelf, rlb;
    void prepare(double fs) {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(Biquad::kPi * f0 / fs);
            const double Vh = std::pow(10.0, G / 20.0);
            const double Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(Biquad::kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            rlb.b0 = 1.0; rlb.b1 = -2.0; rlb.b2 = 1.0;
            rlb.a1 = 2.0 * (K * K - 1.0) / a0;
            rlb.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }
    void reset() { shelf.reset(); rlb.reset(); }
    inline float process(float x) { return rlb.process(shelf.process(x)); }
};

} // namespace bloodline
