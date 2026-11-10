#pragma once
// BloodlineKit - audio-reactive feature block.
// Single writer (audio thread) computes once per block; the editor reads at
// display rate. Relaxed atomics only: the UI never takes a lock the audio
// thread uses, and with the editor closed nothing here runs beyond a few stores.
#include "Biquad.h"
#include <array>
#include <atomic>
#include <cmath>

namespace bloodline {

struct AudioFeatures {
    std::atomic<float> peak{0.f}, rms{0.f}, centroidHz{0.f}, revealed{0.f};
    std::array<std::atomic<float>, 6> bands;
    std::atomic<bool> onset{false};   // editor clears with onset.exchange(false)
    AudioFeatures() { for (auto& b : bands) b.store(0.f); }
};

class FeatureAnalyzer {
public:
    static constexpr std::array<double, 6> kCentres{110.0, 350.0, 950.0, 2500.0, 6000.0, 12000.0};

    void prepare(double fs) {
        for (size_t i = 0; i < 6; ++i) { bp_[i].bandpass(fs, kCentres[i], 1.2); bp_[i].reset(); }
        slow_ = 0.f;
    }

    void process(const float* l, const float* r, int n, AudioFeatures& out) {
        if (n <= 0) return;
        float pk = 0.f; double sumSq = 0.0;
        std::array<double, 6> be{};
        for (int i = 0; i < n; ++i) {
            const float m = 0.5f * (l[i] + r[i]);
            pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
            sumSq += double(m) * m;
            for (size_t b = 0; b < 6; ++b) { const float y = bp_[b].process(m); be[b] += double(y) * y; }
        }
        const float rms = static_cast<float>(std::sqrt(sumSq / n));
        double num = 0.0, den = 1e-12;
        for (size_t b = 0; b < 6; ++b) {
            const float e = static_cast<float>(std::sqrt(be[b] / n));
            out.bands[b].store(e, std::memory_order_relaxed);
            num += kCentres[b] * e; den += e;
        }
        out.peak.store(pk, std::memory_order_relaxed);
        out.rms.store(rms, std::memory_order_relaxed);
        out.centroidHz.store(static_cast<float>(num / den), std::memory_order_relaxed);
        if (rms > 0.01f && rms > 1.5f * slow_) out.onset.store(true, std::memory_order_relaxed);
        slow_ += 0.2f * (rms - slow_);
    }

private:
    std::array<Biquad, 6> bp_;
    float slow_ = 0.f;
};

} // namespace bloodline
