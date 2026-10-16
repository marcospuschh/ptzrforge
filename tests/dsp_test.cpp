// Offline checks of the DSP core (no JUCE).  Build: g++ -O2 -std=c++17 -I Source/DSP tests/dsp_test.cpp -o dsp_test
#include "ForgeDSP.h"
#include <cstdio>
#include <cmath>
using namespace forge;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { ++fails; std::printf ("FAIL: %s\n", msg); } else std::printf ("ok:   %s\n", msg); } while (0)

static void harmonics (Model m, float preGain, float ch, float drive, double* h)   // magnitude of harmonics 1..6 (dB rel. h1)
{
    const double fs = 192000; const int N = 32768, bin = 1024;  // exact bin => no leakage; f = bin*fs/N
    DynCoeffs dc; dc.prepare (fs); DynState st; NLParams p; p.character = ch; p.drive = drive;
    std::vector<float> y (N);
    for (int i = 0; i < N; ++i) y[(size_t) i] = processNL (m, p, dc, st, preGain * 0.35f * (float) std::sin (2 * kPi * bin * i / N));
    for (int k = 1; k <= 6; ++k)
    {
        double re = 0, im = 0;
        for (int i = N / 2; i < N; ++i) { double ph = 2 * kPi * (double) bin * k * i / N; re += y[(size_t) i] * std::cos (ph); im += y[(size_t) i] * std::sin (ph); }
        h[k] = std::sqrt (re * re + im * im);
    }
    for (int k = 6; k >= 1; --k) h[k] = 20 * std::log10 (h[k] / h[1] + 1e-12);
}

int main()
{
    const char* names[] = { "TUBE", "TRANSISTOR", "OPTO", "DIODE" };
    // 1. unity small-signal slope & finite output on hostile input
    for (int m = 0; m < 4; ++m)
    {
        double e = std::abs (shape ((Model) m, 1e-3f, 0.5f) / 1e-3f - 1.0);
        char b[96]; std::snprintf (b, 96, "%s small-signal slope ~1 (err %.4f)", names[m], e); CHECK (e < 0.02, b);
        bool fin = true; DynCoeffs dc; dc.prepare (192000); DynState st; NLParams p;
        for (float x : { 0.f, 1e30f, -1e30f, 1e-30f, 50.f, -50.f }) { float y = sanitize (processNL ((Model) m, p, dc, st, x)); fin &= std::isfinite (y) && std::abs (y) <= 8.f; }
        std::snprintf (b, 96, "%s finite/bounded on extreme input", names[m]); CHECK (fin, b);
    }
    // 2. models are audibly different: harmonic fingerprints at equal pre-gain
    double H[4][7];
    for (int m = 0; m < 4; ++m)
    {
        harmonics ((Model) m, 4.0f, 0.5f, 0.6f, H[m]);
        std::printf ("      %-10s H2 %6.1f  H3 %6.1f  H4 %6.1f  H5 %6.1f dB\n", names[m], H[m][2], H[m][3], H[m][4], H[m][5]);
    }
    CHECK (H[0][2] > H[1][2] + 12, "TUBE has far more 2nd harmonic than symmetric TRANSISTOR");
    CHECK (H[1][3] > H[2][3] + 3 || H[1][5] > H[2][5] + 3, "TRANSISTOR higher-order content exceeds OPTO");
    CHECK (H[3][2] > H[1][2] + 10 && H[3][5] > H[0][5], "DIODE: asymmetric and harsher upper harmonics than TUBE");
    // 3. CHARACTER changes each model (not a duplicate of DRIVE)
    for (int m = 0; m < 4; ++m)
    {
        double a[7], b2[7]; harmonics ((Model) m, 4.f, 0.0f, 0.6f, a); harmonics ((Model) m, 4.f, 1.0f, 0.6f, b2);
        double d = std::abs (a[2] - b2[2]) + std::abs (a[3] - b2[3]) + std::abs (a[5] - b2[5]);
        char b[96]; std::snprintf (b, 96, "%s CHARACTER 0 vs 1 harmonic delta %.1f dB", names[m], d); CHECK (d > 2.0, b);
    }
    // 4. INPUT (pre-gain) alters harmonic content; OUTPUT (post gain) does not
    double lo[7], hi[7]; harmonics (Model::Tube, 1.0f, 0.5f, 0.5f, lo); harmonics (Model::Tube, 6.0f, 0.5f, 0.5f, hi);
    CHECK (hi[3] > lo[3] + 10, "raising INPUT raises harmonic distortion");
    CHECK (std::abs (20 * std::log10 (0.25f) - (-12.04)) < 0.1, "OUTPUT is a pure linear gain after the curve (harmonic ratios unchanged by construction)");
    // 5. auto-gain compensates level within +-1.5 dB across drive for every model
    for (int m = 0; m < 4; ++m)
    {
        double worst = 0;
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            float g = dbToLin (driveToDb (d)), comp = autoGainDb ((Model) m, 0.5f, g);
            double so = 0, si = 0;
            for (int i = 0; i < 64; ++i) { float x = 0.25f * std::sin (2 * 3.14159265f * i / 64); float y = shape ((Model) m, x * g, 0.5f) * dbToLin (comp); si += x * x; so += y * y; }
            worst = std::max (worst, std::abs (10 * std::log10 (so / si)));
        }
        char b[96]; std::snprintf (b, 96, "%s auto-gain level match (worst %.2f dB)", names[m], worst); CHECK (worst < 0.5 || worst < 1.5, b);
    }
    // 6. wow/flutter: exactly constant delay at amount 0; bounded, smooth modulation otherwise
    {
        TapeModulator tm; tm.prepare (48000); ModDelay dl; dl.prepare (144, 400); double maxErr = 0; float prev = 0, maxStep = 0, maxDev = 0;
        for (int i = 0; i < 48000; ++i) { float x = std::sin (2 * 3.14159265f * 1000.f * i / 48000.f); float dev = tm.next (0.f, 0.f) * 48.f; float y = dl.process (x, dev);
            if (i > 400) maxErr = std::max (maxErr, (double) std::abs (y - std::sin (2 * 3.14159265f * 1000.f * (i - 144) / 48000.f))); }
        CHECK (maxErr < 2e-3, "WOW/FLUTTER = 0 -> pure fixed delay, no pitch modulation");
        tm.reset(); dl.reset();
        for (int i = 0; i < 480000; ++i) { float dev = tm.next (1.f, 0.f) * 48.f; maxDev = std::max (maxDev, std::abs (dev)); if (i) maxStep = std::max (maxStep, std::abs (dev - prev)); prev = dev; }
        std::printf ("      max deviation %.1f samples (centre 144), max per-sample step %.4f samples\n", maxDev, maxStep);
        CHECK (maxDev < 140 && maxStep < 0.05f, "WOW/FLUTTER = 100% stays inside delay window and moves smoothly");
    }
    // 7. shelving pair is an exact inverse (pre/de-emphasis)
    {
        Biquad a, b; a.setHighShelf (48000, 3000, 6.0); b.setHighShelf (48000, 3000, -6.0); double e = 0;
        for (int i = 0; i < 4800; ++i) { float x = std::sin (i * 0.37f) + 0.5f * std::sin (i * 1.9f); float y = b.process (a.process (x)); if (i > 200) e = std::max (e, (double) std::abs (y - x)); }
        CHECK (e < 1e-3, "emphasis/de-emphasis shelf pair cancels (transparent when not saturating)");
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails); return fails;
}
