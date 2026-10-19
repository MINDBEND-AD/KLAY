// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/KickPlayer.h"
#include "core/render/TempoDebouncer.h"
#include "core/render/TimeBase.h"
#include "tests/core/TestSignals.h"
#include "tests/rt/RtHarness.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>

using namespace klay;

namespace
{

constexpr double kSr = 48000.0;

struct PlayerFixture
{
    std::map<std::string, std::shared_ptr<const render::SampleData>> samples;
    std::unique_ptr<render::RenderEngine> engine;
    render::KickPlayer player;
    std::array<std::vector<float>, 6> buffers;

    PlayerFixture()
    {
        auto dc = std::make_shared<render::SampleData>();
        dc->sampleRate = kSr;
        dc->audio = render::StereoBuffer(48000);
        std::fill(dc->audio.left.begin(), dc->audio.left.end(), 0.5f);
        std::fill(dc->audio.right.begin(), dc->audio.right.end(), 0.5f);
        samples["dc"] = dc;

        auto tone = std::make_shared<render::SampleData>();
        tone->sampleRate = kSr;
        tone->audio = test::sine(55.0, kSr, 48000, 0.8f); // A1, MIDI 33
        samples["a1"] = tone;

        const render::ResamplerFactory factory = [] { return std::make_unique<test::LinearResampler>(); };
        engine = std::make_unique<render::RenderEngine>(render::RenderEngine::Config{
            [this](const std::string& p) { return samples.at(p); }, factory, true });
        player.prepare(kSr, 512, factory);
        for (auto& b : buffers)
            b.assign(512, 0.0f);
    }

    ~PlayerFixture() { player.reset(*engine, nullptr); }

    void render(const model::Kick& kick)
    {
        REQUIRE(engine->waitForGeneration(engine->requestRender(kick, kSr, 150.0), std::chrono::seconds(10)));
    }

    // Renders n samples, returns the left channel sum over the three buses.
    std::vector<float> play(int n)
    {
        std::vector<float> result;
        while (n > 0)
        {
            const int block = std::min(n, 512);
            for (auto& b : buffers)
                std::fill(b.begin(), b.end(), 0.0f);
            render::KickPlayer::BusOutputs out;
            for (size_t i = 0; i < 3; ++i)
            {
                out.left[i] = buffers[i].data();
                out.right[i] = buffers[i + 3].data();
            }
            player.render(out, block);
            for (int i = 0; i < block; ++i)
                result.push_back(buffers[0][static_cast<size_t>(i)] + buffers[1][static_cast<size_t>(i)] + buffers[2][static_cast<size_t>(i)]);
            n -= block;
        }
        return result;
    }
};

model::Kick dcKick()
{
    auto kick = model::makeDefaultKick();
    kick.settings.autogain = false;
    kick.layers[0].samplePath = "dc";
    return kick;
}

} // namespace

// FR-115 Gherkin: "Given a kick still ringing, when a new note-on arrives, then the old kick fades out within 5 ms
// and the new one starts at full level." FR-001: velocity is ignored (the player has no velocity input).
TEST_CASE("A new note fades the ringing kick within 5 ms; the new one starts at full level", "[core][player][FR-115][FR-001]")
{
    PlayerFixture f;
    auto kick = dcKick();
    f.render(kick);

    f.player.noteOn(*f.engine, nullptr, 60);
    const auto first = f.play(1000);
    CHECK(first[0] == 0.5f);
    CHECK(first[999] == 0.5f);

    f.player.noteOn(*f.engine, nullptr, 60);
    const auto second = f.play(1000);
    const size_t fade = render::msToSamples(5.0, kSr); // 240
    // Old (fading) + new (full) = 0.5 + 0.5 × fade gain.
    CHECK(second[0] == Catch::Approx(0.5f + 0.5f * (1.0f - 1.0f / fade)));
    CHECK(second[fade / 2] == Catch::Approx(0.5f + 0.5f * 0.5f).margin(0.01));
    for (size_t i = fade; i < 1000; ++i)
        REQUIRE(second[i] == 0.5f); // old one gone within 5 ms, new one at full level
}

TEST_CASE("Transport stop fades out within 5 ms", "[core][player][FR-236]")
{
    PlayerFixture f;
    f.render(dcKick());
    f.player.noteOn(*f.engine, nullptr, 60);
    f.play(100);
    f.player.stop();
    const auto out = f.play(500);
    CHECK(out[0] > 0.49f);
    CHECK(out[239] == 0.0f);
    CHECK(out[499] == 0.0f);
    CHECK_FALSE(f.player.isPlaying());
}

// FR-121, FR-125 (fallback path of task 1.12): no key → untransposed; with a key the follow layers are transposed
// relative to the root by real-time Repitch, with zero added latency; fixed layers are unchanged.
TEST_CASE("Key Track transposes follow layers relative to the root", "[core][player][FR-121][FR-125]")
{
    PlayerFixture f;
    auto kick = model::makeDefaultKick();
    kick.settings.autogain = false;
    kick.layers[0].samplePath = "a1";
    kick.layers[0].pitchFollow = true;
    f.render(kick);
    REQUIRE(f.engine->latest()->key.detected);
    CHECK(f.engine->latest()->key.midiNote == 33);

    f.player.noteOn(*f.engine, nullptr, 33);
    CHECK(f.player.lastTranspose() == 0);
    f.player.noteOn(*f.engine, nullptr, 35); // root A1 → note B1: +2 st
    CHECK(f.player.lastTranspose() == 2);
    CHECK_FALSE(f.player.lastFromCache());
    f.play(240); // let the cut voice fade out
    const auto out = f.play(9000);
    CHECK(std::abs(test::cents(test::zeroCrossingHz(out.data(), 0, out.size(), kSr), 55.0 * std::exp2(2.0 / 12.0))) < 5.0);

    // Zero added latency: the transposed kick starts at its first sample.
    // AC-4: output sample j is the input at j × ratio from the note-on on.
    f.player.reset(*f.engine, nullptr);
    f.player.noteOn(*f.engine, nullptr, 40); // +7 st
    const auto start = f.play(64);
    const double ratio = std::exp2(7.0 / 12.0);
    const auto& in = f.engine->latest()->buses[0].follow.left;
    for (size_t j = 0; j < start.size(); ++j)
    {
        const double pos = static_cast<double>(j) * ratio;
        const auto k = static_cast<size_t>(pos);
        const double expected = in[k] + (in[k + 1] - in[k]) * (pos - static_cast<double>(k));
        REQUIRE(start[j] == Catch::Approx(expected).margin(1e-5));
    }
}

TEST_CASE("Player is real-time safe", "[core][player][rt][NFR-001]")
{
    PlayerFixture f;
    auto kick = dcKick();
    kick.layers[1].samplePath = "a1";
    kick.layers[1].pitchFollow = true;
    f.render(kick);

    render::KickPlayer::BusOutputs out;
    for (size_t i = 0; i < 3; ++i)
    {
        out.left[i] = f.buffers[i].data();
        out.right[i] = f.buffers[i + 3].data();
    }
    klay::rt::test::ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        for (int note : { 30, 33, 45, 21, 60, 127, 0 })
        {
            f.player.noteOn(*f.engine, nullptr, note);
            for (int block = 0; block < 4; ++block)
                f.player.render(out, 512);
        }
        f.player.stop();
        f.player.render(out, 512);
    }
    const auto counts = recorder.counts();
    CAPTURE(counts.allocations, counts.deallocations, counts.locks);
    CHECK(counts.total() == 0);
}

// FR-002 (task 1.10) with a simulated playhead: changes settle before a re-render is requested.
TEST_CASE("Host tempo changes are debounced", "[core][time][FR-002]")
{
    render::TempoDebouncer d;
    CHECK_FALSE(d.update(std::nullopt, 0.0).has_value()); // no host tempo yet, nothing changed
    CHECK_FALSE(d.update(128.0, 10.0).has_value());
    CHECK_FALSE(d.update(128.0, 100.0).has_value());
    auto r = d.update(128.0, 170.0);
    REQUIRE(r.has_value());
    CHECK(*r == 128.0);
    CHECK_FALSE(d.update(128.0, 400.0).has_value()); // reported once

    // A ramp only reports the final tempo once it holds still.
    double t = 500.0;
    for (double bpm = 128.0; bpm <= 140.0; bpm += 1.0, t += 20.0)
        CHECK_FALSE(d.update(bpm, t).has_value());
    CHECK_FALSE(d.update(140.0, t + 100.0).has_value());
    r = d.update(140.0, t + 200.0);
    REQUIRE(r.has_value());
    CHECK(*r == 140.0);

    // Jitter below the tolerance is ignored; losing the host tempo is a change too.
    CHECK_FALSE(d.update(140.001, t + 500.0).has_value());
    CHECK_FALSE(d.update(std::nullopt, t + 600.0).has_value());
    r = d.update(std::nullopt, t + 800.0);
    REQUIRE(r.has_value());
    CHECK_FALSE(r->has_value());
}
