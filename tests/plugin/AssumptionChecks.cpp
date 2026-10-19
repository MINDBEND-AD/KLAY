// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
//
// Assumption checks AC-1–AC-4 (spec §12, task 1.16). Each test prints its measurements (WARN) and checks the target
// it confirms. Timing targets (AC-2, AC-3) are enforced locally and only reported on CI (tests/TimingGate.h);
// AC-1 (RAM) and AC-4 (latency) are deterministic and always enforced.
// Results are copied into docs/checks/M1.md. Run alone: klay_tests "[bm]" -s
#include "adapters/JuceResampler.h"
#include "adapters/SampleLoader.h"
#include "core/render/KickEngine.h"
#include "core/render/TimeBase.h"
#include "plugin/KickFiles.h"
#include "plugin/PluginProcessor.h"
#include "tests/TimingGate.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>

using namespace klay;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace
{

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

juce::File fixture(const char* name)
{
    return juce::File(KLAY_TEST_FIXTURES).getChildFile("kicks").getChildFile(name);
}

// A kick with `followBuses` pitch-following layers (body on Main, crunch on A1, rumble on A2) plus a fixed transient.
model::Kick followKick(int followBuses, model::KickLength length)
{
    const auto parts = fixture("samples");
    auto kick = model::makeDefaultKick();
    kick.settings.kickLength = length;
    const char* files[] = { "body.wav", "crunch.wav", "rumble.wav" };
    const model::Routing buses[] = { model::Routing::main, model::Routing::aux1, model::Routing::aux2 };
    for (int i = 0; i < followBuses; ++i)
    {
        auto& layer = kick.layers[static_cast<size_t>(i)];
        layer.samplePath = parts.getChildFile(files[i]).getFullPathName().toStdString();
        layer.routing = buses[i];
        layer.pitchFollow = true;
    }
    kick.layers[4].samplePath = parts.getChildFile("transient.wav").getFullPathName().toStdString();
    return kick;
}

std::unique_ptr<render::KickEngine> makeEngine(double sampleRate, render::KickEngine::CacheConfig cache = {})
{
    return std::make_unique<render::KickEngine>(
        render::RenderEngine::Config{ [sampleRate](const std::string& p) { return adapters::loadSample(p, sampleRate).sample; },
                                      adapters::lagrangeResamplerFactory(), true },
        cache);
}

} // namespace

// AC-1 cache RAM (NFR-014): bytes per cached note = follow buses × kick length × 2 channels × 4 bytes. Checked against
// a real cache, then tabulated for 44.1/48/96 kHz, 1/16…1 bar at 120 BPM (the slowest common tempo), 1–3 buses.
TEST_CASE("AC-1: transposition cache RAM", "[bm][AC-1][NFR-014]")
{
    {
        auto engine = makeEngine(48000.0);
        const auto kick = followKick(3, model::KickLength::quarter);
        REQUIRE(engine->waitForGeneration(engine->requestRender(kick, 48000.0, 120.0), 30s));
        REQUIRE(engine->waitForPrebuild(120s));
        const size_t perNote = 3 * render::kickLengthSamples(model::KickLength::quarter, 120.0, 48000.0) * 2 * sizeof(float);
        CHECK(engine->cachedNotes() == 24);
        CHECK(engine->cacheBytes() == 24 * perNote);
    }

    const double cap = 128.0;
    std::string table = "\nAC-1 cache RAM in MB (per note | root +-12 = 24 notes | all 127 other notes), 120 BPM\n";
    double worst24 = 0.0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (auto length : { model::KickLength::sixteenth, model::KickLength::eighth, model::KickLength::quarter,
                             model::KickLength::half, model::KickLength::bar })
            for (int buses = 1; buses <= 3; ++buses)
            {
                const double mb = buses * static_cast<double>(render::kickLengthSamples(length, 120.0, sr)) * 2 * 4 / (1024.0 * 1024.0);
                if (length != model::KickLength::bar)
                    worst24 = std::max(worst24, 24 * mb);
                char line[160];
                std::snprintf(line, sizeof line, "  %6.1f kHz  %-4s  %d bus  %7.2f | %7.1f | %8.1f%s\n", sr / 1000.0,
                              model::toString(length), buses, mb, 24 * mb, 127 * mb, 127 * mb > cap ? "  (> cap: LRU)" : "");
                table += line;
            }
    WARN(table);
    // Target: the prebuilt root ±12 fits the 128 MB cap for every kick up to 1/2 note (1 bar at 96 kHz × 3 buses does
    // not: there the cap evicts, see docs/checks/M1.md).
    CHECK(worst24 < cap);
}

// AC-2 cache build time (NFR-015): root ±2 < 100 ms, ±12 < 1 s, the full range reported.
TEST_CASE("AC-2: transposition cache build time", "[bm][AC-2][NFR-015]")
{
    std::string report = "\nAC-2 cache build time, 48 kHz, 1/4 at 150 BPM\n";
    double worst2 = 0.0, worst12 = 0.0;
    for (int buses : { 1, 3 })
    {
        auto engine = makeEngine(48000.0);
        const auto kick = followKick(buses, model::KickLength::quarter);
        REQUIRE(engine->waitForGeneration(engine->requestRender(kick, 48000.0, 150.0), 30s));
        const auto t0 = Clock::now();
        const int root = engine->latest()->key.midiNote;
        REQUIRE(root >= 0);
        for (int d : { 1, -1, 2, -2 })
            REQUIRE(engine->waitForCached(root + d, 30s));
        const double ms2 = msSince(t0);
        REQUIRE(engine->waitForPrebuild(120s));
        const double ms12 = msSince(t0);

        // Every other note on first play: request them all at once, as if each had been hit.
        for (int note = 0; note < 128; ++note)
            if (engine->acquire(0, engine->publishedGeneration(), note) != nullptr)
                engine->release(0);
        const auto tAll = Clock::now();
        while (engine->cachedNotes() < 127 && msSince(tAll) < 300000.0)
            std::this_thread::sleep_for(5ms);
        const double msAll = ms12 + msSince(tAll);
        CHECK(engine->cachedNotes() == 127);

        worst2 = std::max(worst2, ms2);
        worst12 = std::max(worst12, ms12);
        char line[200];
        std::snprintf(line, sizeof line, "  %d follow bus(es): root +-2 %.0f ms (target 100) | +-12 %.0f ms (target 1000) | all 127 %.0f ms (%.1f ms/note)\n",
                      buses, ms2, ms12, msAll, (msAll - ms12) / 103.0);
        report += line;
    }
    WARN(report);
    KLAY_TIMING_CHECK(worst2 < 100.0, "AC-2 root +-2 took " << worst2 << " ms, target < 100 ms");
    KLAY_TIMING_CHECK(worst12 < 1000.0, "AC-2 root +-12 took " << worst12 << " ms, target < 1000 ms");
}

// AC-3 playback CPU (NFR-002 partial: no FX chains in M1): 48 kHz / 128-sample blocks, a kick every 400 ms, played
// from the cache or through the real-time Repitch fallback.
TEST_CASE("AC-3: playback CPU, cached and fallback notes", "[bm][AC-3][NFR-002]")
{
    auto engine = makeEngine(48000.0);
    const auto kick = followKick(3, model::KickLength::quarter);
    REQUIRE(engine->waitForGeneration(engine->requestRender(kick, 48000.0, 150.0), 30s));
    REQUIRE(engine->waitForPrebuild(120s));
    const int root = engine->latest()->key.midiNote;

    render::KickPlayer player;
    player.prepare(48000.0, 128, adapters::lagrangeResamplerFactory());
    std::array<std::vector<float>, 6> buffers;
    render::KickPlayer::BusOutputs out;
    for (size_t i = 0; i < 6; ++i)
        buffers[i].assign(128, 0.0f);
    for (size_t i = 0; i < 3; ++i)
    {
        out.left[i] = buffers[i].data();
        out.right[i] = buffers[i + 3].data();
    }

    auto measure = [&](bool cached) {
        const int blocks = 48000 * 10 / 128; // 10 s of audio
        const auto t0 = Clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            if (b % 150 == 0) // every 400 ms
                player.noteOn(*engine, cached ? engine.get() : nullptr, root + 1 + (b / 150) % 11);
            for (auto& buf : buffers)
                std::fill(buf.begin(), buf.end(), 0.0f);
            player.render(out, 128);
        }
        player.reset(*engine, engine.get());
        return msSince(t0) / 10000.0 * 100.0; // % of one core
    };
    const double cachedCpu = measure(true);
    const double fallbackCpu = measure(false);
    WARN("AC-3 playback CPU at 48 kHz / 128, 3 follow buses + fixed: cached " << cachedCpu << " %, Repitch fallback "
         << fallbackCpu << " % of one core (NFR-002 budget for the whole plugin: 5 %)");
    KLAY_TIMING_CHECK(cachedCpu < 1.0, "AC-3 cached playback " << cachedCpu << " % of a core, target < 1 %");
    KLAY_TIMING_CHECK(fallbackCpu < 1.0, "AC-3 fallback playback " << fallbackCpu << " % of a core, target < 1 %");
}

// AC-4 latency (FR-125, NFR-003): through the processor, a note-on at sample 37 gives the first output sample at 37,
// for a cached note and for a fallback note.
TEST_CASE("AC-4: note-on to first sample adds no latency", "[bm][AC-4][NFR-003]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    KlayProcessor processor;
    processor.setPlayConfigDetails(0, 2, 48000.0, 128);
    processor.prepareToPlay(48000.0, 128);
    auto kick = followKick(1, model::KickLength::quarter);
    kick.settings.autogain = true;
    REQUIRE(processor.getEngine().waitForGeneration(processor.setKick(kick), 30s));
    REQUIRE(processor.getEngine().waitForPrebuild(120s));
    const int root = processor.getEngine().latest()->key.midiNote;

    auto firstSound = [&](int note) {
        processor.releaseResources();
        processor.prepareToPlay(48000.0, 128);
        juce::AudioBuffer<float> buffer(2, 128);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 37);
        processor.processBlock(buffer, midi);
        for (int i = 0; i < 128; ++i)
            if (buffer.getSample(0, i) != 0.0f || buffer.getSample(1, i) != 0.0f)
                return i;
        return -1;
    };
    // The body part starts at full level within its first samples, so its first non-zero sample is the note-on.
    const int cached = firstSound(root + 5);
    const int fallback = firstSound(std::min(127, root + 40));
    WARN("AC-4 first output sample after a note-on at 37: cached note " << cached << ", fallback note " << fallback);
    CHECK(cached == 37);
    CHECK(fallback == 37);
}
