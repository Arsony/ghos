#pragma once
// BloodlineKit - linear-phase FIR oversampler (2x / 4x), Kaiser-windowed sinc.
// Up and down filters are symmetric; the down filter is one tap shorter on
// each side so the decimation phase lands on an integer base-rate latency:
//     latency() == 2*K - 1   base-rate samples (K = 16 -> 31 samples).
// The same latency applies to every factor, so switching 2x <-> 4x never
// changes the latency reported to the host.
#include <algorithm>
#include <cmath>
#include <vector>

namespace bloodline {

class Oversampler {
public:
    static constexpr double kPi = 3.14159265358979323846;

    void init(int factor, int k) {
        F = factor; K = k;
        hUp = design(F, F * K);
        hDown = design(F, F * K - 1);
        M = (static_cast<int>(hUp.size()) + F - 1) / F;
        xHist.assign(M, 0.f); xPos = 0;
        Nd = static_cast<int>(hDown.size());
        yHist.assign(Nd, 0.f); yPos = 0;
    }
    void reset() {
        std::fill(xHist.begin(), xHist.end(), 0.f);
        std::fill(yHist.begin(), yHist.end(), 0.f);
        xPos = yPos = 0;
    }
    int factor() const { return F; }
    int latency() const { return 2 * K - 1; }

    // One base-rate sample in, F high-rate samples out.
    void up(float x, float* out) {
        if (++xPos >= M) xPos = 0;
        xHist[xPos] = x;
        const int N = static_cast<int>(hUp.size());
        for (int j = 0; j < F; ++j) {
            float acc = 0.f;
            int idx = xPos;
            for (int k = j; k < N; k += F) {
                acc += hUp[k] * xHist[idx];
                if (--idx < 0) idx = M - 1;
            }
            out[j] = acc * static_cast<float>(F);
        }
    }

    // F high-rate samples in, one base-rate sample out.
    float down(const float* in) {
        for (int j = 0; j < F; ++j) {
            if (++yPos >= Nd) yPos = 0;
            yHist[yPos] = in[j];
        }
        float acc = 0.f;
        int idx = yPos;
        for (int k = 0; k < Nd; ++k) {
            acc += hDown[k] * yHist[idx];
            if (--idx < 0) idx = Nd - 1;
        }
        return acc;
    }

private:
    static double bessel0(double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 60; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    // Length 2*centre+1, cutoff at 90% of the base-rate Nyquist.
    static std::vector<float> design(int F, int centre) {
        const double fc = 0.45 / F;   // cycles per high-rate sample
        const double beta = 9.0, i0b = bessel0(beta);
        const int N = 2 * centre + 1;
        std::vector<double> h(N);
        double sum = 0.0;
        for (int n = 0; n < N; ++n) {
            const double t = n - centre;
            const double s = (t == 0.0) ? 2.0 * fc : std::sin(2.0 * kPi * fc * t) / (kPi * t);
            const double r = t / centre;
            const double w = bessel0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            h[n] = s * w;
            sum += h[n];
        }
        std::vector<float> out(N);
        for (int n = 0; n < N; ++n) out[n] = static_cast<float>(h[n] / sum);
        return out;
    }

    int F = 2, K = 16, M = 0, Nd = 0, xPos = 0, yPos = 0;
    std::vector<float> hUp, hDown, xHist, yHist;
};

} // namespace bloodline
