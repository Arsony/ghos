// GHOST offline engine runner. Pure C++, no JUCE, no audio device.
// Exit code 0 = every check passed. Run via `ctest` or ./ghost_engine_tests.
#include "GhostEngine.h"
#include "bloodline/Loudness.h"
#include "bloodline/Oversampler.h"

#include <chrono>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace bloodline;
using namespace bloodline::ghost;

static int g_fail = 0;
static void check(bool ok, const char* name, const char* detail = "") {
    std::printf("[%s] %s %s\n", ok ? " ok " : "FAIL", name, detail);
    if (!ok) ++g_fail;
}

struct Rng {
    uint64_t s = 88172645463325252ull;
    float next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<float>((s >> 11) * (1.0 / 9007199254740992.0)) * 2.f - 1.f; }
};

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Vocal-ish test signal: harmonic stack with a formant-like tilt, 4 Hz amplitude
// modulation, plus noise bursts in the sibilance band.
static std::vector<float> makeVocal(int n, float peak = 0.3f) {
    std::vector<float> x(n);
    Rng r; Biquad sib; sib.bandpass(kFs, 7000.0, 1.0);
    for (int i = 0; i < n; ++i) {
        const double t = i / kFs;
        double v = 0.0;
        for (int k = 1; k <= 40; ++k) v += std::sin(2 * kPi * 180.0 * k * t) / (k * (1.0 + 0.002 * k * k));
        const double am = 0.5 + 0.5 * std::sin(2 * kPi * 4.0 * t);
        const double burst = (std::fmod(t, 1.0) > 0.8) ? 1.0 : 0.0;
        x[i] = static_cast<float>(v * am * 0.35 + sib.process(r.next()) * 2.0f * burst * 0.2);
    }
    float mx = 0.f; for (float v : x) mx = std::max(mx, std::fabs(v));
    for (float& v : x) v *= peak / mx;
    return x;
}

static Engine makeEngine(const Params& p) {
    Engine e; e.prepare(kFs); e.setParams(p, true); e.reset(); return e;
}

static void run(Engine& e, std::vector<float>& l, std::vector<float>& r, int block = 512) {
    for (size_t i = 0; i < l.size(); i += block) {
        const int n = static_cast<int>(std::min<size_t>(block, l.size() - i));
        e.process(l.data() + i, r.data() + i, n);
    }
}

static void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * kPi / len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
            }
        }
    }
}

static double rmsDb(const std::vector<float>& x, size_t from) {
    double s = 0; for (size_t i = from; i < x.size(); ++i) s += double(x[i]) * x[i];
    return 10.0 * std::log10(s / (x.size() - from) + 1e-30);
}

static double kLoudnessDb(const std::vector<float>& l, const std::vector<float>& r, size_t from) {
    KWeighting a, b; a.prepare(kFs); b.prepare(kFs);
    double s = 0;
    for (size_t i = 0; i < l.size(); ++i) {
        const float x = a.process(l[i]), y = b.process(r[i]);
        if (i >= from) s += double(x) * x + double(y) * y;
    }
    return 10.0 * std::log10(s / (l.size() - from) + 1e-30);
}

int main() {
    char buf[160];
    Engine probe; probe.prepare(kFs);
    const int lat = probe.latencySamples();

    // 1. Oversampler: an impulse through up->down peaks exactly at latency().
    for (int F : {2, 4}) {
        Oversampler os; os.init(F, tune::kOversamplerK);
        std::vector<float> y; float up[4];
        for (int n = 0; n < 200; ++n) { os.up(n == 0 ? 1.f : 0.f, up); y.push_back(os.down(up)); }
        size_t pk = 0; for (size_t i = 0; i < y.size(); ++i) if (std::fabs(y[i]) > std::fabs(y[pk])) pk = i;
        std::snprintf(buf, sizeof buf, "(%dx: peak at %zu, latency() = %d)", F, pk, os.latency());
        check(int(pk) == os.latency(), "oversampler latency matches latency()", buf);
        check(os.latency() == lat, "oversampler latency == engine latency");
    }

    // 2. Null test: MIX 0 must be the input delayed by exactly the reported latency.
    for (bool match : {false, true}) {
        Params p; p.mix = 0.f; p.match = match; p.spirit = p.presence = p.air = p.mist = 1.f;
        Engine e = makeEngine(p);
        auto xl = makeVocal(96000), xr = xl;
        auto l = xl, r = xr; run(e, l, r, 137);
        double maxErr = 0;
        for (size_t i = lat; i < l.size(); ++i) maxErr = std::max(maxErr, double(std::fabs(l[i] - xl[i - lat])));
        std::snprintf(buf, sizeof buf, "(Match %s, max error %.2e)", match ? "on" : "off", maxErr);
        check(maxErr < 1e-6, "null test at MIX 0", buf);
    }

    // 3. Block-size invariance (sample-exact).
    {
        Params p; p.spirit = .8f; p.air = .8f; p.presence = .6f; p.mist = .7f; p.mix = .8f;
        auto x = makeVocal(48000);
        Engine a = makeEngine(p), b = makeEngine(p);
        auto l1 = x, r1 = x, l2 = x, r2 = x;
        run(a, l1, r1, 512); run(b, l2, r2, 33);
        double d = 0; for (size_t i = 0; i < l1.size(); ++i) d = std::max(d, double(std::fabs(l1[i] - l2[i])));
        check(d == 0.0, "output independent of block size");
    }

    // 4. Mono sum: MIST must not cost level in mono. Design doc limit is 1.5 dB.
    {
        Params p; p.spirit = .8f; p.presence = .8f; p.air = .8f; p.mix = 1.f; p.match = false;
        auto x = makeVocal(96000);
        auto sumMono = [&](float mist, double* sideDb) {
            p.mist = mist; Engine e = makeEngine(p);
            auto l = x, r = x; run(e, l, r);
            std::vector<float> m(l.size()), s(l.size());
            for (size_t i = 0; i < l.size(); ++i) { m[i] = l[i] + r[i]; s[i] = l[i] - r[i]; }
            if (sideDb) *sideDb = rmsDb(s, 24000);
            return rmsDb(m, 24000);
        };
        double sideOff, sideOn;
        const double off = sumMono(0.f, &sideOff), on = sumMono(1.f, &sideOn);
        std::snprintf(buf, sizeof buf, "(mono-sum change %.3f dB)", on - off);
        check(std::fabs(on - off) < 1.5, "mono sum loses < 1.5 dB with MIST at 100%", buf);
        std::snprintf(buf, sizeof buf, "(side %.1f dB -> %.1f dB)", sideOff, sideOn);
        check(sideOn > sideOff + 6.0, "MIST actually widens the image", buf);
    }

    // 5. Aliasing floor of the AIR path: sine in, everything not on a harmonic of it is alias.
    for (int os : {2, 4}) {
        Params p; p.spirit = 0.f; p.presence = 0.f; p.air = 1.f; p.mist = 0.f; p.mix = 1.f;
        p.match = false; p.sibGuard = false; p.oversampling = os;
        Engine e = makeEngine(p);
        const double f0 = 7013.0;
        const int N = 65536, total = N + 16384;
        std::vector<float> l(total), r;
        for (int i = 0; i < total; ++i) l[i] = static_cast<float>(0.5 * std::sin(2 * kPi * f0 * i / kFs));
        r = l; run(e, l, r);
        std::vector<std::complex<double>> a(N);
        for (int i = 0; i < N; ++i) {
            const double w = 0.35875 - 0.48829 * std::cos(2 * kPi * i / N) + 0.14128 * std::cos(4 * kPi * i / N) - 0.01168 * std::cos(6 * kPi * i / N);
            a[i] = l[16384 + i] * w;
        }
        fft(a);
        const double binHz = kFs / N;
        auto mag = [&](int k) { return std::abs(a[k]); };
        double fund = 0, other = 0;
        for (int k = 1; k < N / 2; ++k) {
            const double f = k * binHz;
            bool onHarm = false;
            for (int h = 1; h * f0 < kFs / 2; ++h) if (std::fabs(f - h * f0) < 6 * binHz) onHarm = true;
            if (std::fabs(f - f0) < 6 * binHz) fund = std::max(fund, mag(k));
            else if (!onHarm && f > 50.0) other = std::max(other, mag(k));
        }
        const double floorDb = 20.0 * std::log10(other / fund + 1e-30);
        std::snprintf(buf, sizeof buf, "(%dx: alias floor %.1f dB re fundamental; target <= -90)", os, floorDb);
        check(floorDb <= -90.0, "AIR aliasing floor", buf);
    }

    // 6. Loudness match: wet vs dry K-weighted within 0.5 dB.
    {
        Params p; p.spirit = .9f; p.presence = .9f; p.air = .9f; p.mist = .5f; p.mix = 1.f; p.match = true;
        auto x = makeVocal(48000 * 8);
        Engine e = makeEngine(p);
        auto l = x, r = x; run(e, l, r);
        std::vector<float> dl(x.size(), 0.f);
        for (size_t i = lat; i < x.size(); ++i) dl[i] = x[i - lat];
        const size_t from = 48000 * 3;
        const double diff = kLoudnessDb(l, r, from) - kLoudnessDb(dl, dl, from);
        std::snprintf(buf, sizeof buf, "(wet - dry = %.2f dB)", diff);
        check(std::fabs(diff) < 0.5, "Match holds loudness within 0.5 dB", buf);
    }

    // 7. Robustness: silence, extreme and random input stay finite.
    {
        Params p; p.spirit = p.presence = p.air = p.mist = p.mix = 1.f; p.limiter = true; p.oversampling = 4;
        Engine e = makeEngine(p);
        Rng rg; std::vector<float> l(48000), r(48000);
        for (size_t i = 0; i < l.size(); ++i) { const float k = (i < 12000) ? 0.f : (i < 24000 ? 4.f : 1.f); l[i] = rg.next() * k; r[i] = rg.next() * k; }
        run(e, l, r);
        bool finite = true; float mx = 0; for (size_t i = 0; i < l.size(); ++i) { finite &= std::isfinite(l[i]) && std::isfinite(r[i]); mx = std::max(mx, std::max(std::fabs(l[i]), std::fabs(r[i]))); }
        std::snprintf(buf, sizeof buf, "(max out %.3f)", mx);
        check(finite, "no NaN/Inf on silence, +12 dBFS and noise", buf);
        check(mx <= 0.9662f * 1.5f, "limiter keeps output near ceiling", buf);
    }

    // 8. Latency from an impulse with all enhancement off.
    {
        Params p; p.spirit = p.presence = p.air = p.mist = 0.f; p.mix = 1.f; p.match = false; p.sibGuard = false;
        Engine e = makeEngine(p);
        std::vector<float> l(2000, 0.f), r(2000, 0.f); l[100] = 1.f; r[100] = 1.f;
        run(e, l, r);
        size_t pk = 0; for (size_t i = 0; i < l.size(); ++i) if (std::fabs(l[i]) > std::fabs(l[pk])) pk = i;
        std::snprintf(buf, sizeof buf, "(peak at %zu, expected %d)", pk, 100 + lat);
        check(int(pk) == 100 + lat, "reported latency is the real latency", buf);
    }

    // 9. CPU (informational). Worst case: 4x oversampling, everything on.
    {
        Params p; p.spirit = p.presence = p.air = p.mist = p.mix = 1.f; p.oversampling = 4;
        Engine e = makeEngine(p);
        const int secs = 10; auto x = makeVocal(int(kFs) * secs); auto r = x; auto l = x;
        const auto t0 = std::chrono::steady_clock::now();
        run(e, l, r);
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("[info] 4x worst case: %.2f%% of one core at 48 kHz stereo (this machine; plugin target <= 3%% at 2x)\n", 100.0 * el / secs);
        Params p2 = p; p2.oversampling = 2; Engine e2 = makeEngine(p2);
        auto l2 = x, r2 = x;
        const auto t1 = std::chrono::steady_clock::now();
        run(e2, l2, r2);
        const double el2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        std::printf("[info] 2x: %.2f%% of one core\n", 100.0 * el2 / secs);
    }

    std::printf("\n%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
