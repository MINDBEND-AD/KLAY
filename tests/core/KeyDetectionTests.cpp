// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/library/KeyDetector.h"
#include "tests/core/TestSignals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace klay;

namespace
{

// Synthetic kick: sine with an exponential pitch glide from glideHz down to f0 and an exponential decay.
std::vector<float> synthKick(double f0, double sr, double glideHz, double drive, double secondHarmonic = 0.0, double fundamental = 1.0)
{
    const size_t n = static_cast<size_t>(sr * 0.8);
    std::vector<float> x(n);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        const double hz = f0 + (glideHz - f0) * std::exp(-t / 0.02);
        phase += 2.0 * std::numbers::pi * hz / sr;
        double s = fundamental * std::sin(phase) + secondHarmonic * std::sin(2.0 * phase);
        if (drive > 0.0)
            s = std::tanh(drive * s) / std::tanh(drive);
        x[i] = static_cast<float>(0.8 * s * std::exp(-t / 0.35) * std::min(1.0, t / 0.002));
    }
    return x;
}

} // namespace

// FR-120 (task 1.11): synthetic kicks with known f0, detected to ±10 cents.
TEST_CASE("Key detection finds f0 of clean, distorted and gliding kicks", "[core][key][FR-120]")
{
    const double f0 = GENERATE(32.7, 41.2, 46.25, 55.0, 73.4, 98.0, 130.8);
    const double sr = GENERATE(44100.0, 48000.0, 96000.0);
    struct Variant { const char* name; double glide; double drive; };
    const Variant variants[] = { { "clean", 0.0, 0.0 }, { "tanh", 0.0, 4.0 }, { "glide", 4.0, 0.0 }, { "glide+tanh", 3.0, 2.5 } };

    for (const auto& v : variants)
    {
        CAPTURE(f0, sr, v.name);
        const auto x = synthKick(f0, sr, v.glide > 0.0 ? f0 * v.glide : f0, v.drive);
        const auto key = library::detectKey(x.data(), x.size(), sr);
        REQUIRE(key.detected);
        CHECK(std::abs(test::cents(key.hz, f0)) < 10.0);
        CHECK_FALSE(key.uncertain);
        CHECK(key.midiNote == static_cast<int>(std::lround(library::hzToMidi(f0))));
    }
}

TEST_CASE("Key detection flags disagreement between YIN and FFT", "[core][key][FR-120]")
{
    // 45 Hz fundamental under a stronger 90 Hz second harmonic: YIN finds the 45 Hz period, the FFT peak is 90 Hz.
    const auto x = synthKick(45.0, 48000.0, 45.0, 0.0, 1.0, 0.6);
    const auto key = library::detectKey(x.data(), x.size(), 48000.0);
    REQUIRE(key.detected);
    CHECK(std::abs(test::cents(key.hz, 45.0)) < 10.0);
    CHECK(std::abs(test::cents(key.fftHz, 90.0)) < 10.0);
    CHECK(key.uncertain);
}

TEST_CASE("Key detection reports no key for silence and noise-free transients", "[core][key][FR-121]")
{
    std::vector<float> silence(48000, 0.0f);
    CHECK_FALSE(library::detectKey(silence.data(), silence.size(), 48000.0).detected);
    std::vector<float> click(48000, 0.0f);
    click[100] = 1.0f;
    CHECK_FALSE(library::detectKey(click.data(), click.size(), 48000.0).detected);
}

TEST_CASE("Note names and MIDI conversion", "[core][key][FR-123]")
{
    CHECK(library::noteName(60) == "C4");
    CHECK(library::noteName(69) == "A4");
    CHECK(library::noteName(30) == "F#1");
    CHECK(library::noteName(0) == "C-1");
    CHECK(library::midiToHz(69) == 440.0);
    CHECK(std::abs(library::hzToMidi(46.249) - 30.0) < 0.001);
}
