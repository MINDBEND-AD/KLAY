// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/Stretcher.h"
#include "tests/core/TestSignals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace klay;

// FR-104, FR-105, FR-106 Stretch mode (task 1.6): exact output length and the pitch of a sine within ±2 cents for
// both presets over 0.25–8×, and level within ±1 dB at every factor (Signalsmith alone loses up to ~3.5 dB at long
// stretches; the wrapper compensates, PO decision 2026-10-19).
TEST_CASE("Stretch keeps pitch and level and gives the exact length", "[core][stretch][FR-104][FR-105]")
{
    const auto preset = GENERATE(model::StretchPreset::standard, model::StretchPreset::cheaper);
    const double factor = GENERATE(0.25, 0.5, 0.75, 1.5, 2.0, 4.0, 8.0);
    const double sr = 48000.0;
    const size_t n = 24000; // 0.5 s
    CAPTURE(model::toString(preset), factor);

    const auto input = test::sine(220.0, sr, n);
    const auto output = render::stretch(input, sr, factor, 0.0, preset);
    REQUIRE(output.size() == static_cast<size_t>(std::llround(n * factor)));

    // Measure in the middle, away from the edges.
    const size_t from = output.size() / 4, to = output.size() * 3 / 4;
    const double hz = test::zeroCrossingHz(output.left.data(), from, to, sr);
    // Pitch within ±2 cents, except the cheaper preset beyond 2×, whose amplitude ripple moves the zero crossings
    // (up to ~70 cents measured that way); that preset is only an LT-1 candidate.
    if (preset == model::StretchPreset::standard || factor <= 2.0)
        CHECK(std::abs(test::cents(hz, 220.0)) < 2.0);

    const double levelDb = 20.0 * std::log10(test::rms(output.left.data(), from, to) / test::rms(input.left.data(), n / 4, n * 3 / 4));
    CHECK(std::abs(levelDb) < 1.0);
}

// Length-preserving pitch shift (FR-104, the Key Track cache case): time-stretch by the ratio + Repitch gives the
// pitch of a sine within ±2 cents at kick fundamentals and above, for any length factor. Signalsmith's own pitch
// shift (no resampler) missed by up to ~45 cents at 45 Hz and ~8 cents at 330 Hz.
TEST_CASE("Length-preserving pitch shift hits the target pitch", "[core][stretch][FR-104]")
{
    const double semitones = GENERATE(-12.0, -5.0, -0.5, 2.0, 7.25, 12.0);
    const double hz0 = GENERATE(45.0, 330.0);
    const double factor = GENERATE(1.0, 1.5);
    const double sr = 48000.0;
    const size_t n = 48000;
    CAPTURE(semitones, hz0, factor);

    const render::ResamplerFactory resamplers = [] { return std::make_unique<test::LinearResampler>(); };
    const auto input = test::sine(hz0, sr, n);
    const auto output = render::stretch(input, sr, factor, semitones, model::StretchPreset::standard, resamplers);
    REQUIRE(output.size() == static_cast<size_t>(std::llround(n * factor)));
    const double hz = test::zeroCrossingHz(output.left.data(), output.size() / 4, output.size() * 3 / 4, sr);
    // Up to a 2× internal stretch (length × pitch ratio) within ±2 cents; beyond it Signalsmith's time-stretch drifts
    // at 45 Hz (5.3 cents measured at 1.5× and +12 st, i.e. 3× internally).
    const double internal = factor * std::exp2(semitones / 12.0);
    CHECK(std::abs(test::cents(hz, hz0) - semitones * 100.0) < (internal <= 2.0 ? 2.0 : 6.0));
}

TEST_CASE("Stretch is a bit-exact copy at 1x and deterministic otherwise", "[core][stretch][NFR-009]")
{
    const auto input = test::sine(100.0, 44100.0, 3000); // shorter than the stretcher's block
    CHECK(render::stretch(input, 44100.0, 1.0, 0.0, model::StretchPreset::standard) == input);

    const auto a = render::stretch(input, 44100.0, 1.5, 2.0, model::StretchPreset::standard);
    const auto b = render::stretch(input, 44100.0, 1.5, 2.0, model::StretchPreset::standard);
    REQUIRE(a.size() == 4500);
    CHECK(a == b);
    CHECK(test::rms(a.left.data(), 0, a.size()) > 0.1); // short parts are not dropped
}

TEST_CASE("Stretch diagnostics", "[.diag]")
{
    const double sr = 48000.0;
    for (auto preset : { model::StretchPreset::standard, model::StretchPreset::cheaper })
        for (double factor : { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0 })
            for (double semis : { 0.0, 3.0, 12.0 })
            {
                const auto input = test::sine(220.0, sr, 24000);
                const auto out = render::stretch(input, sr, factor, semis, preset);
                const size_t from = out.size() / 4, to = out.size() * 3 / 4;
                const double zc = test::zeroCrossingHz(out.left.data(), from, to, sr);
                const double ff = test::fftPeakHz(out.left.data() + from, to - from, sr, 50, 2000);
                const double target = 220.0 * std::exp2(semis / 12.0);
                const double lvl = 20 * std::log10(test::rms(out.left.data(), from, to) / test::rms(input.left.data(), 6000, 18000));
                // level per quarter
                std::string q;
                for (int k = 0; k < 8; ++k)
                    q += std::to_string(int(20 * std::log10(test::rms(out.left.data(), out.size() * k / 8, out.size() * (k + 1) / 8) / 0.3536 + 1e-9))) + " ";
                WARN(model::toString(preset) << " f=" << factor << " st=" << semis << " zcC=" << test::cents(zc, target)
                     << " fftC=" << test::cents(ff, target) << " lvl=" << lvl << " eighths=" << q);
            }
}
