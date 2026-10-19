// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/RenderEngine.h"
#include "core/render/TimeBase.h"
#include "tests/core/TestSignals.h"
#include "tests/rt/RtHarness.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>
#include <thread>

using namespace klay;
using Catch::Approx;

namespace
{

constexpr double kSr = 48000.0;

std::shared_ptr<const render::SampleData> makeSample(render::StereoBuffer audio)
{
    auto data = std::make_shared<render::SampleData>();
    data->sampleRate = kSr;
    data->audio = std::move(audio);
    return data;
}

render::StereoBuffer impulse(size_t length, size_t at, float value = 1.0f)
{
    render::StereoBuffer b(length);
    b.left[at] = b.right[at] = value;
    return b;
}

render::StereoBuffer ramp(size_t length)
{
    render::StereoBuffer b(length);
    for (size_t i = 0; i < length; ++i)
        b.left[i] = b.right[i] = static_cast<float>(i + 1) / 100000.0f;
    return b;
}

render::StereoBuffer dc(size_t length, float value)
{
    render::StereoBuffer b(length);
    std::fill(b.left.begin(), b.left.end(), value);
    std::fill(b.right.begin(), b.right.end(), value);
    return b;
}

struct Fixture
{
    std::map<std::string, std::shared_ptr<const render::SampleData>> samples;
    render::RenderContext context;

    Fixture()
    {
        context.sampleRate = kSr;
        context.bpm = 150.0; // 1/4 = 400 ms = 19200 samples
        context.samples = [this](const std::string& path) -> std::shared_ptr<const render::SampleData> {
            const auto it = samples.find(path);
            return it == samples.end() ? nullptr : it->second;
        };
        context.resamplers = [] { return std::make_unique<test::LinearResampler>(); };
    }
};

model::Kick plainKick()
{
    auto kick = model::makeDefaultKick();
    kick.settings.autogain = false;
    return kick;
}

float sumAt(const render::RenderedKick& r, size_t i)
{
    float sum = 0.0f;
    for (const auto& bus : r.buses)
        sum += bus.follow.left[i] + bus.fixed.left[i];
    return sum;
}

} // namespace

TEST_CASE("Impulse offset is sample-accurate", "[core][render][FR-102]")
{
    Fixture f;
    f.samples["imp"] = makeSample(impulse(1000, 0));
    auto kick = plainKick();
    kick.layers[0].samplePath = "imp";

    for (double offsetMs : { 0.0, 1.0, 10.5, 123.456, 394.0 }) // the last 5 ms are the end fade
    {
        CAPTURE(offsetMs);
        kick.layers[0].offsetMs = offsetMs;
        const auto r = render::renderKick(kick, f.context);
        REQUIRE(r.length == 19200);
        const size_t expected = render::msToSamples(offsetMs, kSr);
        for (size_t i = 0; i < r.length; ++i)
            REQUIRE(sumAt(r, i) == (i == expected ? 1.0f : 0.0f));
    }
}

TEST_CASE("Start and end trim are exact", "[core][render][FR-102]")
{
    Fixture f;
    f.samples["ramp"] = makeSample(ramp(4800));
    auto kick = plainKick();
    kick.layers[0].samplePath = "ramp";
    kick.layers[0].startTrimMs = 10.0; // 480 samples
    kick.layers[0].endTrimMs = 20.0;   // 960 samples
    kick.layers[0].offsetMs = 1.0;     // 48 samples

    const auto r = render::renderKick(kick, f.context);
    const auto& out = r.buses[0].fixed.left;
    CHECK(out[47] == 0.0f);
    CHECK(out[48] == ramp(4800).left[480]); // first sample after the start trim
    const size_t lastIndex = 48 + (4800 - 480 - 960) - 1;
    CHECK(out[lastIndex] == ramp(4800).left[4800 - 960 - 1]); // last sample before the end trim
    CHECK(out[lastIndex + 1] == 0.0f);
}

TEST_CASE("Mute and solo rules", "[core][render][FR-110][FR-111]")
{
    Fixture f;
    for (int i = 0; i < 6; ++i)
        f.samples["L" + std::to_string(i)] = makeSample(impulse(100, 0, static_cast<float>(1 << i)));
    auto kick = plainKick();
    for (int i = 0; i < 6; ++i)
        kick.layers[static_cast<size_t>(i)].samplePath = "L" + std::to_string(i);

    const auto heard = [&] { return static_cast<int>(sumAt(render::renderKick(kick, f.context), 0)); };
    CHECK(heard() == 0b111111);

    // FR-110: several mutes at once; Shift+click on any Mute unmutes all.
    model::clickMute(kick, 0, false);
    model::clickMute(kick, 3, false);
    CHECK(heard() == 0b110110);
    model::clickMute(kick, 5, true);
    CHECK(heard() == 0b111111);
    model::clickMute(kick, 5, true); // nothing muted: Shift+click mutes this one
    CHECK(heard() == 0b011111);
    model::clickMute(kick, 5, false);

    // FR-111 Gherkin: layer 2 soloed; Solo on layer 3 → only layer 3; with Shift both 2 and 3.
    model::clickSolo(kick, 1, false);
    CHECK(heard() == 0b000010);
    model::clickSolo(kick, 2, false);
    CHECK(heard() == 0b000100);
    model::clickSolo(kick, 1, true);
    CHECK(heard() == 0b000110);

    // Mute beats Solo.
    model::clickMute(kick, 2, false);
    CHECK(heard() == 0b000010);
    model::clickMute(kick, 2, false);

    // Plain click on the only Solo clears it.
    model::clickSolo(kick, 2, false);
    CHECK(heard() == 0b000100);
    model::clickSolo(kick, 2, false);
    CHECK(heard() == 0b111111);
}

TEST_CASE("Layers land on their bus and follow/fixed split", "[core][render][FR-107]")
{
    Fixture f;
    f.samples["imp"] = makeSample(impulse(10, 0));
    auto kick = plainKick();
    const model::Routing routings[] = { model::Routing::main, model::Routing::aux1, model::Routing::aux2 };
    for (size_t i = 0; i < 6; ++i)
    {
        auto& layer = kick.layers[i];
        layer.samplePath = "imp";
        layer.routing = routings[i % 3];
        layer.pitchFollow = i >= 3;
        layer.offsetMs = static_cast<double>(i); // separates the layers in time
    }
    const auto r = render::renderKick(kick, f.context);
    for (size_t i = 0; i < 6; ++i)
    {
        CAPTURE(i);
        const size_t at = render::msToSamples(static_cast<double>(i), kSr);
        for (size_t b = 0; b < 3; ++b)
        {
            const bool here = b == i % 3;
            CHECK(r.buses[b].follow.left[at] == ((here && i >= 3) ? 1.0f : 0.0f));
            CHECK(r.buses[b].fixed.left[at] == ((here && i < 3) ? 1.0f : 0.0f));
        }
    }
}

TEST_CASE("Fader, envelope, kick length, end fade and autogain", "[core][render][FR-101][FR-108][FR-109][FR-285]")
{
    Fixture f;
    f.samples["dc"] = makeSample(dc(48000, 0.5f));
    auto kick = plainKick();
    kick.layers[0].samplePath = "dc";
    kick.layers[0].faderDb = -6.0f;
    kick.layers[0].envelope.points = { { 0.0, 1.0f, 0.0f }, { 200.0, 0.5f, 0.0f } };

    auto r = render::renderKick(kick, f.context);
    const auto& out = r.buses[0].fixed.left;
    const float fader = std::pow(10.0f, -6.0f / 20.0f);
    CHECK(out[0] == Approx(0.5f * fader));
    CHECK(out[4800] == Approx(0.5f * fader * 0.75f)); // 100 ms: half-way down the linear segment
    CHECK(out[9600] == Approx(0.5f * fader * 0.5f));
    CHECK(out[r.length - 1] == 0.0f); // FR-108 end fade
    CHECK(out[r.length - 241] == Approx(0.5f * fader * 0.5f)); // just before the 5 ms fade

    kick.layers[0].faderDb = model::kMinusInfinityDb;
    CHECK(sumAt(render::renderKick(kick, f.context), 0) == 0.0f);

    // FR-108: the length follows note value and BPM.
    kick.settings.kickLength = model::KickLength::bar;
    f.context.bpm = 120.0;
    CHECK(render::renderKick(kick, f.context).length == 96000);

    // FR-109: one gain from the summed buses to −0.3 dBFS, applied to every bus alike.
    kick = plainKick();
    kick.settings.autogain = true;
    kick.layers[0].samplePath = "dc";
    kick.layers[1].samplePath = "dc";
    kick.layers[1].routing = model::Routing::aux1;
    kick.layers[1].faderDb = -12.0f;
    f.context.bpm = 150.0;
    kick.settings.kickLength = model::KickLength::quarter;
    r = render::renderKick(kick, f.context);
    const float target = std::pow(10.0f, -0.3f / 20.0f);
    const float peak = 0.5f + 0.5f * std::pow(10.0f, -12.0f / 20.0f);
    CHECK(r.autogain == Approx(target / peak));
    CHECK(sumAt(r, 100) == Approx(target));
    CHECK(r.buses[1].fixed.left[100] / r.buses[0].fixed.left[100] == Approx(std::pow(10.0f, -12.0f / 20.0f)));
}

TEST_CASE("Two renders are bit-identical", "[core][render][NFR-009]")
{
    Fixture f;
    f.samples["a"] = makeSample(test::sine(55.0, kSr, 20000));
    f.samples["b"] = makeSample(test::sine(3000.0, kSr, 3000));
    auto kick = model::makeDefaultKick();
    kick.layers[0].samplePath = "a";
    kick.layers[0].length = 1.3;
    kick.layers[2].samplePath = "a";
    kick.layers[2].mode = model::LayerMode::pitch;
    kick.layers[2].pitchSemitones = -3;
    kick.layers[1].samplePath = "b";
    kick.layers[1].mode = model::LayerMode::tape;
    kick.layers[1].tapeSpeed = 1.33;
    kick.layers[1].pitchFollow = true;
    kick.layers[1].envelope.points = { { 0.0, 1.0f, 0.4f }, { 50.0, 0.0f, 0.0f } };

    const auto a = render::renderKick(kick, f.context);
    f.context.parallel = false;
    const auto b = render::renderKick(kick, f.context);
    for (size_t i = 0; i < 3; ++i)
    {
        CHECK(a.buses[i].follow == b.buses[i].follow);
        CHECK(a.buses[i].fixed == b.buses[i].fixed);
    }
    CHECK(a.autogain == b.autogain);
}

TEST_CASE("Missing samples are reported and stay silent", "[core][render][FR-003]")
{
    Fixture f;
    auto kick = plainKick();
    kick.layers[0].samplePath = "gone.wav";
    kick.layers[1].samplePath = "gone.wav";
    kick.layers[2].samplePath = "also-gone.wav";
    kick.layers[2].mute = true; // still reported
    const auto r = render::renderKick(kick, f.context);
    CHECK(r.missingSamples == std::vector<std::string>{ "gone.wav", "also-gone.wav" });
    CHECK(sumAt(r, 0) == 0.0f);
}

// FR-107, NFR-013, NFR-001 (task 1.8): edits on one thread while the audio thread reads; every buffer the reader
// sees is complete and consistent, and the reader never allocates, frees or locks.
TEST_CASE("Render engine swaps renders lock-free under edits", "[core][render][rt][NFR-013][NFR-001]")
{
    std::map<std::string, std::shared_ptr<const render::SampleData>> samples;
    samples["dc"] = makeSample(dc(48000, 0.5f));
    render::RenderEngine engine({ [&](const std::string& p) { return samples.at(p); }, nullptr, true });

    std::atomic<bool> done{ false };
    std::atomic<long> reads{ 0 }, bad{ 0 };
    klay::rt::test::Counts counts{};
    std::thread audio([&] {
        klay::rt::test::ScopedViolationRecorder recorder;
        while (! done.load())
        {
            RT_GUARD;
            if (const auto* k = engine.acquire(0))
            {
                // Every sample before the end fade equals the same fader-dependent value: a torn or freed buffer
                // would break this.
                const float v = k->buses[0].fixed.left[0];
                const size_t fadeStart = k->length - render::msToSamples(5.0, k->sampleRate);
                for (size_t i = 0; i < fadeStart; i += 7)
                    if (k->buses[0].fixed.left[i] != v || k->buses[0].fixed.right[i] != v)
                        bad.fetch_add(1);
                reads.fetch_add(1);
            }
            engine.release(0);
        }
        counts = recorder.counts();
    });

    auto kick = plainKick();
    kick.layers[0].samplePath = "dc";
    std::uint64_t last = 0;
    for (int edit = 0; edit < 300; ++edit)
    {
        kick.layers[0].faderDb = -static_cast<float>(edit % 40);
        kick.settings.kickLength = edit % 2 ? model::KickLength::eighth : model::KickLength::sixteenth;
        last = engine.requestRender(kick, kSr, 150.0);
        if (edit % 10 == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(engine.waitForGeneration(last, std::chrono::seconds(10)));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    done = true;
    audio.join();

    CHECK(reads.load() > 0);
    CHECK(bad.load() == 0);
    CHECK(counts.total() == 0);
    const auto latest = engine.latest();
    REQUIRE(latest);
    CHECK(latest->generation == last);
    CHECK(latest->buses[0].fixed.left[0] == Approx(0.5f * std::pow(10.0f, -(299 % 40) / 20.0f)));
}

// NFR-004 (task 1.8 BM): 12 layers, 1 beat at 150 BPM, re-render after an edit < 30 ms. The edit is a fader move,
// so processed parts come from the layer cache; a cold render (every part through Signalsmith) is reported too.
TEST_CASE("Benchmark: re-render of 12 layers after an edit", "[core][render][bm][NFR-004]")
{
    std::map<std::string, std::shared_ptr<const render::SampleData>> samples;
    for (int i = 0; i < 12; ++i)
        samples["s" + std::to_string(i)] = makeSample(test::sine(40.0 + 10.0 * i, kSr, 24000));

    auto kick = model::makeDefaultKick();
    kick.layers.resize(12);
    for (int i = 0; i < 12; ++i)
    {
        auto& layer = kick.layers[static_cast<size_t>(i)];
        layer.samplePath = "s" + std::to_string(i);
        layer.mode = i % 2 ? model::LayerMode::pitch : model::LayerMode::length;
        layer.length = 1.0 + 0.1 * i;
        layer.pitchSemitones = i % 5 - 2;
        layer.pitchFollow = i % 2 == 0;
        layer.routing = static_cast<model::Routing>(i % 3);
        layer.envelope.points = { { 0.0, 1.0f, 0.0f }, { 300.0, 0.2f, 0.5f } };
    }

    render::RenderEngine engine({ [&](const std::string& p) { return samples.at(p); }, nullptr, true });
    using clock = std::chrono::steady_clock;

    auto t0 = clock::now();
    REQUIRE(engine.waitForGeneration(engine.requestRender(kick, kSr, 150.0), std::chrono::seconds(30)));
    const double coldMs = std::chrono::duration<double, std::milli>(clock::now() - t0).count();

    double worstMs = 0.0;
    for (int edit = 0; edit < 10; ++edit)
    {
        kick.layers[static_cast<size_t>(edit)].faderDb = -3.0f;
        t0 = clock::now();
        REQUIRE(engine.waitForGeneration(engine.requestRender(kick, kSr, 150.0), std::chrono::seconds(30)));
        worstMs = std::max(worstMs, std::chrono::duration<double, std::milli>(clock::now() - t0).count());
    }
    WARN("NFR-004: re-render after an edit, worst of 10: " << worstMs << " ms (target < 30 ms); cold render: " << coldMs << " ms");
    CHECK(worstMs < 30.0);
}

// FR-104–106 (spec v0.10, task 1.20): one knob, three modes; only the active mode's value is applied.
TEST_CASE("Only the active layer mode is applied", "[core][render][FR-104][FR-105][FR-106]")
{
    Fixture f;
    f.samples["sine"] = makeSample(test::sine(220.0, kSr, 9600));
    const auto& input = f.samples["sine"]->audio;
    auto layer = model::makeDefaultLayer(0);
    layer.samplePath = "sine";
    // Every mode holds a value; only the active one may show in the output.
    layer.length = 2.0;
    layer.pitchSemitones = 7;
    layer.pitchCents = 30;
    layer.tapeSpeed = 0.5;
    const auto hz = [](const render::StereoBuffer& b) { return test::zeroCrossingHz(b.left.data(), b.size() / 4, b.size() * 3 / 4, kSr); };

    // Length: length × 2, pitch kept (FR-105).
    layer.mode = model::LayerMode::length;
    auto part = render::processPart(layer, input, f.context, model::StretchPreset::standard);
    CHECK(part.size() == 19200);
    CHECK(std::abs(test::cents(hz(part), 220.0)) < 2.0);

    // Pitch: +7.3 st exact, length kept (FR-104).
    layer.mode = model::LayerMode::pitch;
    part = render::processPart(layer, input, f.context, model::StretchPreset::standard);
    CHECK(part.size() == 9600);
    CHECK(std::abs(test::cents(hz(part), 220.0) - 730.0) < 2.0);

    // Tape: speed 0.5 = twice as long and one octave down, together.
    layer.mode = model::LayerMode::tape;
    part = render::processPart(layer, input, f.context, model::StretchPreset::standard);
    CHECK(part.size() == 19200);
    CHECK(std::abs(test::cents(hz(part), 110.0)) < 2.0);
    CHECK(render::activeSemitones(layer) == -12.0);

    // Neutral value of the active mode: the trimmed sample, bit for bit, whatever the other modes hold.
    for (auto mode : { model::LayerMode::length, model::LayerMode::pitch, model::LayerMode::tape })
    {
        auto neutral = layer;
        neutral.mode = mode;
        neutral.length = mode == model::LayerMode::length ? 1.0 : 4.0;
        neutral.pitchSemitones = mode == model::LayerMode::pitch ? 0 : -9;
        neutral.pitchCents = mode == model::LayerMode::pitch ? 0 : 50;
        neutral.tapeSpeed = mode == model::LayerMode::tape ? 1.0 : 3.0;
        CHECK(render::processPart(neutral, input, f.context, model::StretchPreset::standard) == input);
    }
}
