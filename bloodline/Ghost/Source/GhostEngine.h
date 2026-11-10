#pragma once
// GHOST - vocal presence engine. Plain C++17, no JUCE: the plugin wraps it and
// tests/run_engine.cpp drives it offline.
//
// Topology note: the dry path is never split or filtered. Every enhancement
// (SPIRIT / PRESENCE / AIR) is generated from filtered copies and ADDED to a
// latency-aligned dry signal, so the lows are untouched by construction and no
// crossover phase-compensation is needed. The 250 Hz / 2 kHz / 6 kHz edges from
// the design doc appear as the filter edges of each path.
//
// All constants in `tune::` are STARTING VALUES. They were never auditioned;
// expect to tweak them by ear in FL Studio.
#include "bloodline/Biquad.h"
#include "bloodline/Loudness.h"
#include "bloodline/Oversampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bloodline::ghost {

enum class Voice { Auto = 0, Male, Female, DryRap };

struct Params {
    float spirit = 0.35f, presence = 0.40f, air = 0.30f, mist = 0.25f, mix = 0.70f; // 0..1
    bool sibGuard = true;
    Voice voice = Voice::Auto;
    int oversampling = 2;      // 2 or 4
    float outTrimDb = 0.f;     // -12..+12
    bool match = true;
    bool limiter = false;      // soft limiter at -0.3 dBFS
};

namespace tune {
constexpr float kSpDrive = 4.0f, kSpBias = 0.3f, kSpMakeup = 1.5f;
constexpr float kRevealInv = 20.0f;      // reveal gain = 1 / (1 + env * 20): ref level ~ -26 dBFS
constexpr float kPresGain = 1.0f;        // parallel band-pass add of 1.0 == +6 dB at the centre
constexpr float kShelf = 0.585f;         // 10 kHz parallel shelf, +4 dB at full AIR
constexpr float kAirDrive = 6.0f, kAirGen = 0.25f;   // 4x drive
constexpr float kAirDrive2x = 3.0f;   // 2x is gentler: 6.0 measured -65 dB alias floor, 3.0 clears -90 dB
constexpr float kSibSens = 20.0f;        // duck = 1 / (1 + sibEnv * 20)
constexpr float kMistAp = 0.6f;
constexpr int kOversamplerK = 16;        // latency 2K-1 = 31 samples, any factor
} // namespace tune

class Engine {
public:
    static constexpr double kPi = 3.14159265358979323846;

    int latencySamples() const { return 2 * tune::kOversamplerK - 1; }
    float revealedEnergy() const { return revealed_; }

    // Allocates. Call off the audio thread.
    void prepare(double sampleRate) {
        fs_ = sampleRate;
        const int lat = latencySamples();
        for (auto& c : ch_) {
            c.spHP.highpass(fs_, 800.0);   c.spLP.lowpass(fs_, 6000.0);
            c.harmHP.highpass(fs_, 1500.0);
            c.air6.highpass(fs_, 6000.0);  c.air10.highpass(fs_, 10000.0);
            c.airHP.highpass(fs_, 10000.0);
            c.airLP.lowpass(fs_, std::min(18000.0, 0.45 * fs_));
            c.prBP.bandpass(fs_, 3500.0, 0.9);
            c.kDry.prepare(fs_); c.kOut.prepare(fs_);
            c.os2.init(2, tune::kOversamplerK); c.os4.init(4, tune::kOversamplerK);
            c.dry.init(lat);
            c.tapBuf.init(static_cast<int>(0.036 * fs_));
        }
        sibHP_.highpass(fs_, 5000.0); sibLP_.lowpass(fs_, 9000.0);
        static constexpr std::array<double, 6> apMs{3.1, 5.3, 7.9, 11.7, 15.3, 19.7};
        for (size_t s = 0; s < 6; ++s) ap_[s].init(std::max(1, static_cast<int>(apMs[s] * 0.001 * fs_)), 0.5f);
        mistTap_.init(static_cast<int>(0.036 * fs_));
        tapD_ = {static_cast<int>(0.011 * fs_), static_cast<int>(0.023 * fs_), static_cast<int>(0.034 * fs_)};

        auto coef = [&](double t) { return static_cast<float>(1.0 - std::exp(-1.0 / (t * fs_))); };
        atkLoud_ = coef(0.010); relLoud_ = coef(0.150);
        atkSib_ = coef(0.001);  relSib_ = coef(0.030);
        atkSp_ = coef(0.005);   relSp_ = coef(0.080);
        cLoudness_ = coef(0.400);
        cRev_ = coef(0.050);
        spirit_.coef = coef(0.050);
        for (Smooth* s : {&presence_, &air_, &mist_, &mix_, &trim_}) s->coef = coef(0.020);

        biasTanh_ = std::tanh(tune::kSpBias);
        setParams(Params{}, true);
        reset();
    }

    void reset() {
        for (auto& c : ch_) {
            for (Biquad* b : {&c.spHP, &c.spLP, &c.harmHP, &c.air6, &c.air10, &c.airHP, &c.airLP, &c.prBP}) b->reset();
            c.kDry.reset(); c.kOut.reset(); c.os2.reset(); c.os4.reset();
            c.dry.clear(); c.tapBuf.clear();
        }
        sibHP_.reset(); sibLP_.reset();
        for (auto& a : ap_) a.clear();
        mistTap_.clear();
        loudEnv_ = sibEnv_ = spEnv_ = revealed_ = revealSq_ = 0.f;
        msDry_ = msOut_ = 0.f;
    }

    // Cheap and audio-thread safe. snap=true jumps smoothers (used by tests/prepare).
    void setParams(const Params& p, bool snap = false) {
        spirit_.tgt = p.spirit; presence_.tgt = p.presence; air_.tgt = p.air;
        mist_.tgt = p.mist; mix_.tgt = p.mix;
        trim_.tgt = std::pow(10.0f, p.outTrimDb / 20.0f);
        guard_ = p.sibGuard; match_ = p.match; limiter_ = p.limiter;

        const int os = (p.oversampling >= 4) ? 4 : 2;
        if (os != os_) { os_ = os; for (auto& c : ch_) { c.os2.reset(); c.os4.reset(); } }

        double centre = 3500.0;
        switch (p.voice) {
            case Voice::Auto: centre = 3500.0; break;
            case Voice::Male: centre = 2800.0; break;
            case Voice::Female: centre = 4200.0; break;
            case Voice::DryRap: centre = 3200.0; break;
        }
        if (centre != presCentre_) {
            presCentre_ = centre;
            for (auto& c : ch_) c.prBP.bandpass(fs_, centre, 0.9);
        }
        if (snap) for (Smooth* s : {&spirit_, &presence_, &air_, &mist_, &mix_, &trim_}) s->cur = s->tgt;
    }

    void process(float* L, float* R, int n) {
        using namespace tune;
        const int F = os_;
        std::array<float, 4> upv{};
        for (int i = 0; i < n; ++i) {
            const float spirit = spirit_.next(), pres = presence_.next(), air = air_.next();
            const float mist = mist_.next(), mix = mix_.next(), trim = trim_.next();
            const float in[2] = {L[i], R[i]};

            // Stereo-linked detectors on the mid signal.
            const float mid = 0.5f * (in[0] + in[1]);
            const float am = std::fabs(mid);
            loudEnv_ += (am > loudEnv_ ? atkLoud_ : relLoud_) * (am - loudEnv_);
            const float sb = std::fabs(sibLP_.process(sibHP_.process(mid)));
            sibEnv_ += (sb > sibEnv_ ? atkSib_ : relSib_) * (sb - sibEnv_);
            const float duck = guard_ ? 1.0f / (1.0f + sibEnv_ * kSibSens) : 1.0f;

            float band[2];
            for (int c = 0; c < 2; ++c) band[c] = ch_[c].spLP.process(ch_[c].spHP.process(in[c]));
            const float bm = std::fabs(0.5f * (band[0] + band[1]));
            spEnv_ += (bm > spEnv_ ? atkSp_ : relSp_) * (bm - spEnv_);
            const float revealGain = 1.0f / (1.0f + spEnv_ * kRevealInv);
            const float presLevel = 1.0f - 0.6f * std::min(loudEnv_ / 0.2f, 1.0f);

            float add[2], dry[2];
            float gen = 0.f;
            for (int c = 0; c < 2; ++c) {
                Chan& ch = ch_[c];
                const float x = in[c];

                // SPIRIT: biased tanh -> harmonics only -> inverse level follower.
                const float sat = std::tanh(kSpDrive * band[c] + kSpBias) - biasTanh_;
                const float harm = ch.harmHP.process(sat) * revealGain * spirit * kSpMakeup;

                // PRESENCE: parallel band-pass == dynamic bell, lifts more when quiet.
                const float pr = ch.prBP.process(x) * pres * presLevel * kPresGain;

                // AIR: oversampled saturation of the >6 kHz band, band-limited to 10-18 kHz.
                const float hf = ch.air6.process(x);
                Oversampler& os = (F == 4) ? ch.os4 : ch.os2;
                os.up(hf, upv.data());
                for (int j = 0; j < F; ++j) upv[j] = std::tanh((F == 2 ? kAirDrive2x : kAirDrive) * upv[j]);
                const float sd = os.down(upv.data());
                const float airGen = ch.airLP.process(ch.airHP.process(sd)) * air * kAirGen;
                const float shelf = ch.air10.process(x) * air * kShelf;

                add[c] = (harm + pr + airGen + shelf) * duck;
                dry[c] = ch.dry.process(x, latencySamples());
                gen += harm * harm + airGen * airGen;
            }
            revealSq_ += cRev_ * (0.5f * gen - revealSq_);
            revealed_ = std::sqrt(revealSq_);

            // MIST: decorrelate only the generated mid, into the side channel.
            // L+R of the added signal is unchanged, so the mono sum cannot lose level.
            const float wm = 0.5f * (add[0] + add[1]);
            const float ws = 0.5f * (add[0] - add[1]);
            float d = wm;
            for (auto& a : ap_) d = a.process(d);
            mistTap_.write(wm);
            const float amb = 0.30f * mistTap_.read(tapD_[0]) + 0.20f * mistTap_.read(tapD_[1])
                            + 0.15f * mistTap_.read(tapD_[2]);
            const float side = ws + mist * (kMistAp * d + amb);
            const float wet[2] = {dry[0] + wm + side, dry[1] + wm - side};

            // MIX: equal-power parallel, then loudness-match the result to the dry.
            const float cd = std::cos(mix * static_cast<float>(kPi) * 0.5f);
            const float sw = std::sin(mix * static_cast<float>(kPi) * 0.5f);
            float out[2];
            float kd = 0.f, ko = 0.f;
            for (int c = 0; c < 2; ++c) {
                out[c] = cd * dry[c] + sw * wet[c];
                const float a = ch_[c].kDry.process(dry[c]);
                const float b = ch_[c].kOut.process(out[c]);
                kd += a * a; ko += b * b;
            }
            msDry_ += cLoudness_ * (kd - msDry_);
            msOut_ += cLoudness_ * (ko - msOut_);
            float g = 1.0f;
            if (match_) g = std::min(2.0f, std::max(0.5f, std::sqrt((msDry_ + 1e-9f) / (msOut_ + 1e-9f))));

            for (int c = 0; c < 2; ++c) {
                float y = out[c] * g * trim;
                if (limiter_) y = softLimit(y);
                (c == 0 ? L : R)[i] = y;
            }
        }
    }

private:
    struct Smooth {
        float cur = 0.f, tgt = 0.f, coef = 0.f;
        inline float next() { cur += coef * (tgt - cur); return cur; }
    };

    struct DelayLine {
        std::vector<float> buf; int pos = 0, size = 1;
        void init(int maxDelay) { size = maxDelay + 1; buf.assign(size, 0.f); pos = 0; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.f); pos = 0; }
        inline void write(float x) { if (++pos >= size) pos = 0; buf[pos] = x; }
        inline float read(int d) const { int i = pos - d; if (i < 0) i += size; return buf[i]; }
        inline float process(float x, int d) { write(x); return read(d); }
    };

    struct Allpass {   // Schroeder allpass: (-g + z^-D) / (1 - g z^-D)
        std::vector<float> buf; int pos = 0; float g = 0.5f;
        void init(int d, float gain) { buf.assign(d, 0.f); pos = 0; g = gain; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.f); pos = 0; }
        inline float process(float x) {
            const float wd = buf[pos];
            const float w = x + g * wd;
            buf[pos] = w;
            if (++pos >= static_cast<int>(buf.size())) pos = 0;
            return -g * w + wd;
        }
    };

    struct Chan {
        Biquad spHP, spLP, harmHP, air6, air10, airHP, airLP, prBP;
        KWeighting kDry, kOut;
        Oversampler os2, os4;
        DelayLine dry, tapBuf;
    };

    static float softLimit(float x) {
        constexpr float T = 0.8f, C = 0.9661f;   // ceiling = -0.3 dBFS
        const float a = std::fabs(x);
        if (a <= T) return x;
        const float y = T + (C - T) * std::tanh((a - T) / (C - T));
        return x < 0 ? -y : y;
    }

    double fs_ = 48000.0, presCentre_ = 0.0;
    std::array<Chan, 2> ch_;
    Biquad sibHP_, sibLP_;
    std::array<Allpass, 6> ap_;
    DelayLine mistTap_;
    std::array<int, 3> tapD_{};
    Smooth spirit_, presence_, air_, mist_, mix_, trim_;
    float atkLoud_ = 0, relLoud_ = 0, atkSib_ = 0, relSib_ = 0, atkSp_ = 0, relSp_ = 0, cLoudness_ = 0, cRev_ = 0;
    float loudEnv_ = 0, sibEnv_ = 0, spEnv_ = 0, revealed_ = 0, revealSq_ = 0, msDry_ = 0, msOut_ = 0, biasTanh_ = 0;
    int os_ = 2;
    bool guard_ = true, match_ = true, limiter_ = false;
};

} // namespace bloodline::ghost
