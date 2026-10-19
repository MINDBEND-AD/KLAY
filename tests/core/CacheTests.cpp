// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/library/KeyDetector.h"
#include "core/render/KickEngine.h"
#include "tests/core/TestSignals.h"
#include "tests/rt/RtHarness.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>

using namespace klay;
using namespace std::chrono_literals;

namespace
{

constexpr double kSr = 48000.0;
constexpr int kNoteF1 = 29; // 43.65 Hz
constexpr int kNoteG1 = 31;

struct CacheFixture
{
    std::map<std::string, std::shared_ptr<const render::SampleData>> samples;
    std::unique_ptr<render::KickEngine> engine;
    render::KickPlayer player;
    std::array<std::vector<float>, 6> buffers;
    render::KickPlayer::BusOutputs out;

    explicit CacheFixture(render::KickEngine::CacheConfig cacheConfig = {})
    {
        add("f1", test::sine(library::midiToHz(kNoteF1), kSr, 24000, 0.8f));
        add("transient", test::sine(3000.0, kSr, 2000, 0.5f));
        const render::ResamplerFactory factory = [] { return std::make_unique<test::LinearResampler>(); };
        engine = std::make_unique<render::KickEngine>(
            render::RenderEngine::Config{ [this](const std::string& p) { return samples.at(p); }, factory, true }, cacheConfig);
        player.prepare(kSr, 512, factory);
        for (size_t i = 0; i < 6; ++i)
            buffers[i].assign(512, 0.0f);
        for (size_t i = 0; i < 3; ++i)
        {
            out.left[i] = buffers[i].data();
            out.right[i] = buffers[i + 3].data();
        }
    }
    ~CacheFixture() { player.reset(*engine, engine.get()); }

    void add(const std::string& name, render::StereoBuffer audio)
    {
        auto data = std::make_shared<render::SampleData>();
        data->sampleRate = kSr;
        data->audio = std::move(audio);
        samples[name] = data;
    }

    // Key layer F1 following the key on Main; a fixed Transient layer on Aux 1.
    model::Kick kick() const
    {
        auto k = model::makeDefaultKick();
        k.settings.autogain = false;
        k.layers[0].samplePath = "f1";
        k.layers[0].pitchFollow = true;
        k.layers[4].samplePath = "transient";
        k.layers[4].routing = model::Routing::aux1;
        return k;
    }

    void render(const model::Kick& k)
    {
        REQUIRE(engine->waitForGeneration(engine->requestRender(k, kSr, 150.0), 20s));
    }

    void clear()
    {
        for (auto& b : buffers)
            std::fill(b.begin(), b.end(), 0.0f);
    }
};

} // namespace

// Spec §12 Gherkin: "Given Key Track on and a detected root of F, when MIDI note G is played, then pitch-following
// layers sound 2 semitones up without added delay, and the Transient layer is unchanged."
TEST_CASE("Root F, note G: follow layers +2 st from the cache, Transient unchanged", "[core][cache][FR-121][FR-125]")
{
    CacheFixture f;
    f.render(f.kick());
    const auto kick = f.engine->latest();
    REQUIRE(kick->key.detected);
    REQUIRE(kick->key.midiNote == kNoteF1);
    REQUIRE(f.engine->waitForCached(kNoteG1, 20s));

    f.player.noteOn(*f.engine, f.engine.get(), kNoteG1);
    CHECK(f.player.lastTranspose() == 2);
    CHECK(f.player.lastFromCache());

    std::vector<float> follow, transient;
    for (int block = 0; block < 30; ++block)
    {
        f.clear();
        f.player.render(f.out, 512);
        follow.insert(follow.end(), f.buffers[0].begin(), f.buffers[0].end());
        transient.insert(transient.end(), f.buffers[1].begin(), f.buffers[1].end());
    }

    // Transient (fixed, Aux 1) bit-identical to the render.
    for (size_t i = 0; i < transient.size() && i < kick->length; ++i)
        REQUIRE(transient[i] == kick->buses[1].fixed.left[i]);

    // Follow layers 2 st up, exact pitch (time-stretch + Repitch).
    const double target = library::midiToHz(kNoteG1);
    CHECK(std::abs(test::cents(test::zeroCrossingHz(follow.data(), 2000, 14000, kSr), target)) < 2.0);
}

// FR-125, NFR-003, AC-4: the first output sample is the cached buffer's first sample, at the note-on.
TEST_CASE("Cached note adds no latency", "[core][cache][FR-125][NFR-003]")
{
    CacheFixture f;
    f.render(f.kick());
    REQUIRE(f.engine->waitForPrebuild(30s));
    f.player.noteOn(*f.engine, f.engine.get(), kNoteF1 + 5);
    REQUIRE(f.player.lastFromCache());
    const auto* entry = f.engine->acquire(1, f.engine->publishedGeneration(), kNoteF1 + 5); // test reads via spare slot
    REQUIRE(entry != nullptr);
    f.clear();
    f.player.render(f.out, 64);
    for (size_t i = 0; i < 64; ++i)
        REQUIRE(f.buffers[0][i] == entry->follow[0].left[i]);
    f.engine->release(1);
}

TEST_CASE("Prebuild covers root +-12 nearest first; other notes on first play", "[core][cache][FR-125][NFR-015]")
{
    CacheFixture f;
    f.render(f.kick());
    REQUIRE(f.engine->waitForPrebuild(60s));
    CHECK(f.engine->cachedNotes() == 24);
    for (int d = 1; d <= 12; ++d)
    {
        CHECK(f.engine->isCached(kNoteF1 + d));
        CHECK(f.engine->isCached(kNoteF1 - d));
    }
    CHECK_FALSE(f.engine->isCached(kNoteF1 + 20));

    // First play of +20: Repitch fallback now, cached for the next hit.
    f.player.noteOn(*f.engine, f.engine.get(), kNoteF1 + 20);
    CHECK_FALSE(f.player.lastFromCache());
    REQUIRE(f.engine->waitForCached(kNoteF1 + 20, 20s));
    f.player.noteOn(*f.engine, f.engine.get(), kNoteF1 + 20);
    CHECK(f.player.lastFromCache());

    // A new render invalidates the cache, then rebuilds it.
    auto k = f.kick();
    k.layers[0].faderDb = -3.0f;
    f.render(k);
    REQUIRE(f.engine->waitForPrebuild(60s));
    CHECK(f.engine->cachedNotes() == 24);
    CHECK_FALSE(f.engine->isCached(kNoteF1 + 20));
}

// NFR-014: the cache stays under its byte cap; a played note evicts the least recently played ones.
TEST_CASE("Cache eviction under the byte cap", "[core][cache][NFR-014]")
{
    // One entry = 1 follow bus × 19200 samples × 2 ch × 4 bytes = 153.6 kB; cap = 3 entries.
    const size_t entry = 19200 * 2 * sizeof(float);
    CacheFixture f({ entry * 3, 12, model::StretchPreset::standard });
    f.render(f.kick());
    REQUIRE(f.engine->waitForPrebuild(30s));
    CHECK(f.engine->cachedNotes() == 3);
    CHECK(f.engine->cacheBytes() <= entry * 3);
    CHECK(f.engine->isCached(kNoteF1 + 1));
    CHECK(f.engine->isCached(kNoteF1 - 1));
    CHECK(f.engine->isCached(kNoteF1 + 2));

    // Play +1 and +2 (recently used), then a note that is not cached: −1 (never played) is evicted.
    for (int note : { kNoteF1 + 1, kNoteF1 + 2 })
        f.player.noteOn(*f.engine, f.engine.get(), note);
    f.player.noteOn(*f.engine, f.engine.get(), kNoteF1 + 7);
    REQUIRE(f.engine->waitForCached(kNoteF1 + 7, 20s));
    CHECK(f.engine->cacheBytes() <= entry * 3);
    CHECK_FALSE(f.engine->isCached(kNoteF1 - 1));
    CHECK(f.engine->isCached(kNoteF1 + 1));
    CHECK(f.engine->isCached(kNoteF1 + 2));
}

TEST_CASE("No key or no follow layers: nothing is cached, notes play untransposed", "[core][cache][FR-121]")
{
    CacheFixture f;
    auto k = f.kick();
    k.layers[0].pitchFollow = false;
    f.render(k);
    REQUIRE(f.engine->waitForPrebuild(10s));
    CHECK(f.engine->cachedNotes() == 0);

    k.layers[0].samplePath.clear(); // the key layer is empty: no key detected
    k.layers[4].pitchFollow = true;
    f.render(k);
    CHECK_FALSE(f.engine->latest()->key.detected);
    f.player.noteOn(*f.engine, f.engine.get(), 70);
    CHECK(f.player.lastTranspose() == 0);
}

TEST_CASE("Cache lookups from the audio thread are real-time safe", "[core][cache][rt][NFR-001]")
{
    CacheFixture f;
    f.render(f.kick());
    REQUIRE(f.engine->waitForPrebuild(30s));
    klay::rt::test::ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        for (int note : { kNoteF1 + 2, kNoteF1 - 12, kNoteF1 + 30, 127, 0, kNoteF1 })
        {
            f.player.noteOn(*f.engine, f.engine.get(), note); // hits and misses
            for (int block = 0; block < 4; ++block)
                f.player.render(f.out, 512);
        }
    }
    const auto counts = recorder.counts();
    CAPTURE(counts.allocations, counts.deallocations, counts.locks);
    CHECK(counts.total() == 0);
}
