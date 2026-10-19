// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/Envelope.h"
#include "core/render/TimeBase.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

using namespace klay;
using model::KickLength;
using Catch::Approx;

// FR-108: note value × BPM → ms → samples.
TEST_CASE("Kick length table: note values at several tempi", "[core][time][FR-108]")
{
    struct Row { KickLength length; double bpm; double ms; };
    const Row rows[] = {
        { KickLength::sixteenth, 120.0, 125.0 },  { KickLength::eighth, 120.0, 250.0 },
        { KickLength::quarter, 120.0, 500.0 },    { KickLength::half, 120.0, 1000.0 },
        { KickLength::bar, 120.0, 2000.0 },       { KickLength::quarter, 150.0, 400.0 },
        { KickLength::quarter, 155.0, 60000.0 / 155.0 }, { KickLength::sixteenth, 175.0, 60000.0 / 175.0 / 4.0 },
        { KickLength::bar, 60.0, 4000.0 },        { KickLength::eighth, 140.0, 60000.0 / 140.0 / 2.0 },
    };
    for (const auto& r : rows)
    {
        CAPTURE(model::toString(r.length), r.bpm);
        CHECK(render::kickLengthMs(r.length, r.bpm) == Approx(r.ms));
        CHECK(render::kickLengthSamples(r.length, r.bpm, 48000.0) == static_cast<size_t>(std::llround(r.ms * 48.0)));
        CHECK(render::kickLengthSamples(r.length, r.bpm, 44100.0) == static_cast<size_t>(std::llround(r.ms * 44.1)));
    }
    CHECK(render::kickLengthSamples(KickLength::quarter, 150.0, 48000.0) == 19200);
}

// FR-002: host tempo unless the manual override is on or no host tempo is present.
TEST_CASE("Effective BPM follows the host unless overridden", "[core][time][FR-002]")
{
    model::Settings s;
    CHECK(render::effectiveBpm(s, std::nullopt) == 155.0);
    CHECK(render::effectiveBpm(s, 128.0) == 128.0);
    CHECK(render::effectiveBpm(s, 0.0) == 155.0);
    s.manualBpmOverride = true;
    s.manualBpm = 140.0;
    CHECK(render::effectiveBpm(s, 128.0) == 140.0);
}

// FR-108 (Q8): 5 ms raised-cosine fade to exactly zero.
TEST_CASE("End fade is 5 ms, monotonic and ends at zero", "[core][time][FR-108]")
{
    const double sr = GENERATE(44100.0, 48000.0, 96000.0);
    const size_t fade = render::msToSamples(5.0, sr);
    CHECK(fade == static_cast<size_t>(std::llround(sr * 0.005)));

    std::vector<float> data(fade * 3, 1.0f);
    render::applyEndFade(data.data(), data.size(), sr);
    for (size_t i = 0; i < fade * 2; ++i)
        REQUIRE(data[i] == 1.0f); // untouched before the fade
    for (size_t i = fade * 2 + 1; i < data.size(); ++i)
        REQUIRE(data[i] <= data[i - 1]);
    CHECK(data.back() == 0.0f);
    CHECK(data[fade * 2] > 0.99f);
    CHECK(data[fade * 2 + fade / 2 - 1] == Approx(0.5).margin(0.02)); // half-way ≈ −6 dB (cosine shape)

    // Shorter than the fade: the whole buffer fades, still ends at zero.
    std::vector<float> tiny(3, 1.0f);
    render::applyEndFade(tiny.data(), tiny.size(), sr);
    CHECK(tiny.back() == 0.0f);
}

// FR-282 Gherkin: "Given a 4×4 grid, when an envelope point is dragged without Alt, then it lands on the nearest
// grid intersection."
TEST_CASE("Envelope points snap to the nearest grid intersection", "[core][envelope][FR-282]")
{
    const double length = 400.0; // 1/4 at 150 BPM
    auto snapped = render::snapToGrid({ 130.0, 0.6f, 0.0f }, length, 4, false);
    CHECK(snapped.timeMs == Approx(100.0));
    CHECK(snapped.gain == Approx(0.5f));

    snapped = render::snapToGrid({ 160.0, 0.9f, 0.0f }, length, 4, false);
    CHECK(snapped.timeMs == Approx(200.0));
    CHECK(snapped.gain == Approx(1.0f));

    snapped = render::snapToGrid({ 130.0, 0.6f, 0.0f }, length, 8, false);
    CHECK(snapped.timeMs == Approx(150.0));
    CHECK(snapped.gain == Approx(0.625f));

    snapped = render::snapToGrid({ 130.0, 0.6f, 0.0f }, length, 16, false);
    CHECK(snapped.timeMs == Approx(125.0));
    CHECK(snapped.gain == Approx(0.625f));

    // Alt: free placement, only clamped to the grid area.
    snapped = render::snapToGrid({ 130.0, 0.6f, 0.3f }, length, 4, true);
    CHECK(snapped == model::EnvelopePoint{ 130.0, 0.6f, 0.3f });
    snapped = render::snapToGrid({ 900.0, 1.4f, 0.0f }, length, 4, true);
    CHECK(snapped.timeMs == 400.0);
    CHECK(snapped.gain == 1.0f);
}

TEST_CASE("Envelope values at and between points", "[core][envelope][FR-280][FR-281]")
{
    model::Envelope env;
    CHECK(render::envelopeValue(env, 0.0) == 1.0f); // FR-280: flat at 100 %
    CHECK(render::envelopeValue(env, 1234.0) == 1.0f);

    env.points = { { 0.0, 1.0f, 0.0f }, { 100.0, 0.0f, 0.5f }, { 200.0, 1.0f, -0.5f }, { 300.0, 0.25f, 0.0f } };
    CHECK(render::envelopeValue(env, 0.0) == 1.0f);
    CHECK(render::envelopeValue(env, 100.0) == 0.0f);
    CHECK(render::envelopeValue(env, 200.0) == 1.0f);
    CHECK(render::envelopeValue(env, 300.0) == 0.25f);
    CHECK(render::envelopeValue(env, 500.0) == 0.25f); // holds after the last point
    // A segment takes the tension of its start point.
    CHECK(render::envelopeValue(env, 50.0) == Approx(0.5f)); // tension 0: linear
    CHECK(render::envelopeValue(env, 150.0) == Approx(std::pow(0.5, std::pow(8.0, 0.5))));
    CHECK(render::envelopeValue(env, 250.0) == Approx(1.0 - 0.75 * (1.0 - std::pow(0.5, std::pow(8.0, 0.5)))));

    // Tension bends the curve within the segment's range (Q10).
    for (float t : { -1.0f, -0.3f, 0.0f, 0.7f, 1.0f })
        for (double x = 0.0; x <= 1.0; x += 0.05)
        {
            const float v = render::curveValue(0.2f, 0.8f, t, x);
            REQUIRE(v >= 0.2f - 1e-6f);
            REQUIRE(v <= 0.8f + 1e-6f);
        }
    CHECK(render::curveValue(0.0f, 1.0f, 1.0f, 0.5) < 0.5f);
    CHECK(render::curveValue(0.0f, 1.0f, -1.0f, 0.5) > 0.5f);
}

// FR-285: sample-accurate and click-free; FR-283: points keep absolute time when the kick length changes.
TEST_CASE("Rendered envelope has no discontinuities and keeps absolute time", "[core][envelope][FR-283][FR-285]")
{
    model::Envelope env;
    env.points = { { 0.0, 1.0f, 0.0f }, { 20.0, 0.1f, 0.8f }, { 21.0, 0.9f, -0.9f }, { 200.0, 0.0f, 0.0f } };
    const double sr = 48000.0;
    std::vector<float> gains(render::msToSamples(400.0, sr));
    render::renderEnvelope(env, sr, 0, gains.data(), gains.size());

    // Steepest legal segment: 0.8 over 1 ms (48 samples); with tension 0.9 the curve may be up to ~6.5× steeper
    // than linear at its end, so a step stays well under a full-scale jump.
    float maxStep = 0.0f;
    for (size_t i = 1; i < gains.size(); ++i)
        maxStep = std::max(maxStep, std::abs(gains[i] - gains[i - 1]));
    CHECK(maxStep < 0.12f);

    CHECK(gains[0] == 1.0f);
    CHECK(gains[render::msToSamples(20.0, sr)] == Approx(0.1f));
    CHECK(gains[render::msToSamples(200.0, sr)] == 0.0f);

    // Same envelope at two kick lengths: identical gain at the same absolute time.
    std::vector<float> shortKick(render::msToSamples(100.0, sr));
    render::renderEnvelope(env, sr, 0, shortKick.data(), shortKick.size());
    for (size_t i = 0; i < shortKick.size(); ++i)
        REQUIRE(shortKick[i] == gains[i]);

    // Rendering from an offset gives the same values (sample-accurate block rendering).
    std::vector<float> tail(1000);
    render::renderEnvelope(env, sr, 500, tail.data(), tail.size());
    for (size_t i = 0; i < tail.size(); ++i)
        REQUIRE(tail[i] == gains[500 + i]);
}
