#pragma once
// PTZR.FORGE DSP core. Pure C++17 (no JUCE) so it can be unit-tested offline.
// None of the processing functions allocate; ModDelay::prepare() allocates and must be called off the audio thread.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace forge
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float x)  { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }
inline float sanitize (float x) { return (x == x) ? std::clamp (x, -8.0f, 8.0f) : 0.0f; }   // NaN -> 0, hard-bounded
inline float smoothstep01 (float x) { x = std::clamp (x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

enum class Model { Tube = 0, Transistor, Opto, Diode };

//==============================================================================
// RBJ biquad (transposed direct form II, double precision state)
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    float process (float x) noexcept
    {
        const double xd = x, y = b0 * xd + z1;
        z1 = b1 * xd - a1 * y + z2;
        z2 = b2 * xd - a2 * y;
        return (float) y;
    }
    void reset() noexcept { z1 = z2 = 0; }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }
    void setHighShelf (double fs, double f, double gDb, double S = 0.8) noexcept { shelf (fs, f, gDb, S, true); }
    void setLowShelf  (double fs, double f, double gDb, double S = 0.8) noexcept { shelf (fs, f, gDb, S, false); }

    void setPeak (double fs, double f, double gDb, double Q) noexcept
    {
        const double A = std::pow (10.0, gDb / 40.0), w = 2 * kPi * f / fs, al = std::sin (w) / (2 * Q), c = std::cos (w);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setLowpass (double fs, double f, double Q = 0.7071) noexcept
    {
        const double w = 2 * kPi * std::min (f, fs * 0.45) / fs, al = std::sin (w) / (2 * Q), c = std::cos (w);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
private:
    void shelf (double fs, double f, double gDb, double S, bool high) noexcept
    {
        const double A = std::pow (10.0, gDb / 40.0), w = 2 * kPi * std::min (f, fs * 0.45) / fs;
        const double c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt ((A + 1 / A) * (1 / S - 1) + 2), t = 2 * std::sqrt (A) * al;
        if (high)
            set (A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - t),
                 (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
        else
            set (A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
                 (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    }
};

// First-order DC blocker (~ 12 Hz)
struct DcBlocker
{
    float x1 = 0, y1 = 0, R = 0.998f;
    void setup (double fs) { R = (float) std::exp (-2.0 * kPi * 12.0 / fs); }
    float process (float x) noexcept { const float y = x - x1 + R * y1; x1 = x; y1 = std::abs (y) < 1e-20f ? 0.f : y; return y1; }
    void reset() { x1 = y1 = 0; }
};

//==============================================================================
// Static transfer functions. All have unity small-signal slope so that DRIVE/INPUT are the only
// things that decide how hard the curve is hit.  c = CHARACTER in 0..1.
inline float shape (Model m, float x, float c) noexcept
{
    switch (m)
    {
        case Model::Tube:        // asymmetric (biased) soft curve -> 2nd/3rd harmonic dominant, progressive onset
        {
            const float b  = 0.08f + 0.34f * c;                      // CHARACTER = grid bias / even-harmonic amount
            const float tb = std::tanh (b);
            return (std::tanh (x + b) - tb) / (1.0f - tb * tb);
        }
        case Model::Transistor:  // symmetric, sharpening knee: x / (1+|x|^p)^(1/p) -> odd harmonics, tighter onset
        {
            const float p = 2.0f + 6.0f * c;                         // CHARACTER = knee hardness
            return x / std::pow (1.0f + std::pow (std::abs (x), p), 1.0f / p);
        }
        case Model::Opto:        // very gentle rational curve (static part); dynamics do most of the work
        {
            const float k = 0.6f + 0.6f * c;
            return x / (1.0f + k * std::abs (x));
        }
        case Model::Diode:       // exponential diode-like clipper, asymmetric forward/reverse knees
        {
            const float k    = 1.0f + 1.6f * c;                      // CHARACTER = knee steepness (hardness)
            const float asym = 0.55f + 0.3f * (1.0f - c);
            if (x >= 0.0f) return (1.0f - std::exp (-k * x)) / k;
            return -(1.0f - std::exp (-k * asym * -x)) / (k * asym);
        }
    }
    return x;
}

//==============================================================================
// Level-dependent (stateful) part of the nonlinear stage.
struct DynCoeffs
{
    float attFast = 0, relFast = 0, attSlow = 0, relSlow = 0,   // transient / sag detectors
          attO1 = 0, relO1 = 0, attO2 = 0, relO2 = 0, attTube = 0, relTube = 0;
    static float coef (double t, double fs) { return (float) std::exp (-1.0 / (t * fs)); }
    void prepare (double fs)   // fs = rate the nonlinear stage runs at
    {
        attFast = coef (0.0002, fs); relFast = coef (0.004, fs);
        attSlow = coef (0.005, fs);  relSlow = coef (0.060, fs);
        attO1 = coef (0.010, fs);    relO1 = coef (0.120, fs);
        attO2 = coef (0.040, fs);    relO2 = coef (1.100, fs);
        attTube = coef (0.004, fs);  relTube = coef (0.130, fs);
    }
};

struct DynState { float eF = 0, eS = 0, o1 = 0, o2 = 0, tube = 0; };

inline float follow (float env, float a, float att, float rel) noexcept
{
    const float k = a > env ? att : rel;
    return k * env + (1.0f - k) * a;
}

struct NLParams
{
    float character = 0.5f;   // 0..1
    float drive = 0.0f;       // 0..1 (used to scale dynamic amounts)
};

// One sample through model m (state is updated). x is already multiplied by the total pre-gain.
inline float processNL (Model m, const NLParams& p, const DynCoeffs& d, DynState& s, float x) noexcept
{
    const float a = std::abs (x), c = p.character;
    switch (m)
    {
        case Model::Tube:         // "sag": slow-ish level-dependent gain reduction ahead of the curve
        {
            s.tube = follow (s.tube, a, d.attTube, d.relTube);
            const float sag = (0.10f + 0.45f * c) * (0.4f + 0.6f * p.drive);
            return shape (m, x / (1.0f + sag * s.tube), c);
        }
        case Model::Transistor:   // transient emphasis ahead of a tight knee: attacks poke into the curve harder
        {
            s.eF = follow (s.eF, a, d.attFast, d.relFast);
            s.eS = follow (s.eS, a, d.attSlow, d.relSlow);
            const float tr = std::clamp (s.eF / (s.eS + 1.0e-4f) - 1.0f, 0.0f, 2.0f);
            return shape (m, x * (1.0f + (0.15f + 0.55f * c) * tr), c);
        }
        case Model::Opto:         // program-dependent two-stage compression (fast+slow release), then gentle rounding
        {
            s.o1 = follow (s.o1, a, d.attO1, d.relO1);
            s.o2 = follow (s.o2, a, d.attO2, d.relO2);
            const float env  = 0.55f * s.o1 + 0.45f * s.o2;
            const float comp = 0.35f + 1.6f * c;
            return shape (m, x / (1.0f + comp * env), c);
        }
        case Model::Diode:        // instantaneous, hard, asymmetric; a touch of drive-dependent bite
            return shape (m, x, c);
    }
    return x;
}

// Auto-gain: static RMS ratio of a reference sine (-12 dBFS peak) through the shaper at pre-gain g.
inline float autoGainDb (Model m, float character, float preGain) noexcept
{
    constexpr int N = 64; constexpr float A = 0.25f;
    double si = 0, so = 0;
    for (int i = 0; i < N; ++i)
    {
        const float x = A * std::sin (2.0f * (float) kPi * (float) i / N);
        const float y = shape (m, x * preGain, character);
        si += (double) x * x; so += (double) y * y;
    }
    const float db = 0.5f * (float) std::log10 ((si + 1e-12) / (so + 1e-12)) * 20.0f;   // 10*log10(ratio)
    return std::clamp (db, -24.0f, 12.0f);
}

//==============================================================================
// Tape speed modulation: wow (slow) + flutter (fast) as a smooth modulated delay.
struct TapeModulator
{
    double fs = 44100, ph[5] = { 0, 0.21, 0.47, 0.13, 0.66 };
    uint32_t rng = 0x1234567u;
    float nW = 0, nWt = 0, nF = 0, nFt = 0;      // smoothed random walks
    int cnt = 0;

    void prepare (double sampleRate) { fs = sampleRate; reset(); }
    void reset() { rng = 0x1234567u; nW = nWt = nF = nFt = 0; cnt = 0; }
    float rnd() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) * (1.0f / 8388608.0f) - 1.0f; }

    // returns delay deviation in milliseconds for amount in 0..1; chOffset decorrelates channels slightly
    float next (float amount, float chOffset) noexcept
    {
        if ((++cnt & 255) == 0) { nWt = rnd(); nFt = rnd(); }
        nW += (nWt - nW) * (float) (1.0 / (fs * 0.9));     // ~0.9 s random drift
        nF += (nFt - nF) * (float) (1.0 / (fs * 0.045));   // ~45 ms jitter
        const double f[4] = { 0.55, 1.13, 6.9, 11.7 };
        double s[4];
        for (int i = 0; i < 4; ++i)
        {
            ph[i] += f[i] / fs; if (ph[i] >= 1.0) ph[i] -= 1.0;
            s[i] = std::sin (2.0 * kPi * (ph[i] + (i >= 2 ? chOffset : chOffset * 0.2)));
        }
        const float wow = (float) (0.65 * s[0] + 0.25 * s[1]) + 0.35f * nW;
        const float flu = (float) (0.55 * s[2] + 0.30 * s[3]) + 0.30f * nF;
        // Response curve: amount^1.6 so the first half of the knob is subtle; flutter ramps in later than wow.
        const float w = std::pow (amount, 1.6f), fl = std::pow (amount, 2.0f);
        return w * 1.15f * wow + fl * 0.060f * flu;        // max wow ~ +-1.5 ms @0.5 Hz (~0.5 %), flutter ~ +-0.07 ms @ 7-12 Hz
    }
};

// Fixed-centre modulated delay with 4-point Hermite interpolation.
struct ModDelay
{
    std::vector<float> buf; int mask = 0, w = 0; float centre = 0;
    void prepare (int centreSamples, int maxSamples)
    {
        int n = 1; while (n < maxSamples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; centre = (float) centreSamples;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    float process (float x, float devSamples) noexcept
    {
        buf[(size_t) w] = x;
        const float d = std::max (centre + devSamples, 2.0f);
        const float rp = (float) w - d;
        const int   i  = (int) std::floor (rp); const float f = rp - (float) i;
        const float y0 = buf[(size_t) ((i - 1) & mask)], y1 = buf[(size_t) (i & mask)],
                    y2 = buf[(size_t) ((i + 1) & mask)], y3 = buf[(size_t) ((i + 2) & mask)];
        const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        w = (w + 1) & mask;
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
};

// Control mappings shared by processor and tests
inline float driveToDb (float d01)  { return 28.0f * std::pow (std::clamp (d01, 0.0f, 1.0f), 1.15f); }
}
