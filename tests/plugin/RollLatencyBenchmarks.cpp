// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
//
// Task 2.6, NFR-005: Forge to audible result < 100 ms. "Audible" = the render holding the newly drawn samples is
// published to the audio thread (the next note-on or audition plays it). Measured cold: every Forge runs on a fresh
// instance, so the drawn files are not in its sample cache and are read, decoded and resampled to the session rate.
// The synthetic library runs everywhere; set KLAY_BM_LIBRARY to a library folder (e.g. the factory staging folder) to
// measure on real samples too. Run alone: klay_tests "[NFR-005]" -s
#include "plugin/PluginProcessor.h"
#include "tests/TimingGate.h"
#include "tests/plugin/LibraryFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <vector>

using namespace klay;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace
{

struct LatencyStats
{
    double median = 0.0;
    double max = 0.0;
    int rolls = 0;
};

// `rolls` cold Forges, each on a new instance prepared at 48 kHz; the layers in the template are the default 1–3
// unless `allLayers` (every one of the six layers rolled: the worst case).
LatencyStats measureForge(const std::filesystem::path& root, const std::filesystem::path& dataDir, int rolls, bool allLayers)
{
    std::vector<double> ms;
    for (int r = 0; r < rolls; ++r)
    {
        KlayProcessor processor(ProcessorOptions{ root, dataDir });
        processor.prepareToPlay(48000.0, 512);
        REQUIRE(processor.getLibrary().waitUntilReady(120s));
        if (allLayers)
            processor.edit("Template", [](model::Kick& k) { k.settings.rollTemplate = { 0, 1, 2, 3, 4, 5 }; });
        REQUIRE(processor.getEngine().waitForGeneration(processor.getRequestedGeneration(), 10s));

        const auto t0 = Clock::now();
        const auto result = processor.forge(static_cast<std::uint64_t>(r) * 7919u + 1u);
        REQUIRE_FALSE(result.changed.empty());
        REQUIRE(processor.getEngine().waitForGeneration(processor.getRequestedGeneration(), 10s));
        ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());

        const auto latest = processor.getEngine().latest();
        REQUIRE(latest);
        CHECK(latest->missingSamples.empty());
    }
    std::sort(ms.begin(), ms.end());
    return { ms[ms.size() / 2], ms.back(), rolls };
}

} // namespace

TEST_CASE("NFR-005: Forge to audible < 100 ms (synthetic library, cold sample cache)", "[bm][NFR-005]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::SyntheticLibrary lib(20, 1.0, 44100.0); // 1 s parts at 44.1 kHz: decoded and resampled to 48 kHz
    {
        KlayProcessor indexer(ProcessorOptions{ lib.root(), lib.dataDir() });
        REQUIRE(indexer.getLibrary().waitUntilReady(120s));
    }

    const auto three = measureForge(lib.root(), lib.dataDir(), 10, false);
    const auto six = measureForge(lib.root(), lib.dataDir(), 5, true);
    WARN("NFR-005 Forge to audible, layers 1-3, cold: median " << three.median << " ms, max " << three.max << " ms over "
                                                            << three.rolls << " rolls (target < 100 ms)");
    WARN("NFR-005 Forge to audible, all 6 layers, cold: median " << six.median << " ms, max " << six.max << " ms over "
                                                              << six.rolls << " rolls (target < 100 ms)");
    KLAY_TIMING_CHECK(three.max < 100.0, "Forge layers 1-3 max " << three.max << " ms");
    KLAY_TIMING_CHECK(six.max < 100.0, "Forge 6 layers max " << six.max << " ms");
}

TEST_CASE("NFR-005: Forge to audible on the library in KLAY_BM_LIBRARY", "[bm][NFR-005]")
{
    const auto folder = juce::SystemStats::getEnvironmentVariable("KLAY_BM_LIBRARY", {});
    if (folder.isEmpty())
        SKIP("KLAY_BM_LIBRARY not set");
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File data = juce::File::createTempFile("klay-bm-data");
    data.createDirectory();
    const auto root = library::fromUtf8(folder.toStdString());
    const auto dataDir = library::fromUtf8(data.getFullPathName().toStdString());
    {
        KlayProcessor indexer(ProcessorOptions{ root, dataDir });
        REQUIRE(indexer.getLibrary().waitUntilReady(300s));
        REQUIRE(indexer.getRolePools().total() > 0);
    }
    const auto three = measureForge(root, dataDir, 10, false);
    const auto six = measureForge(root, dataDir, 5, true);
    data.deleteRecursively();
    WARN("NFR-005 Forge to audible on " << folder << ", layers 1-3, cold: median " << three.median << " ms, max "
                                        << three.max << " ms (target < 100 ms)");
    WARN("NFR-005 Forge to audible on " << folder << ", all 6 layers, cold: median " << six.median << " ms, max "
                                        << six.max << " ms (target < 100 ms)");
    KLAY_TIMING_CHECK(three.max < 100.0, "Forge layers 1-3 max " << three.max << " ms");
    KLAY_TIMING_CHECK(six.max < 100.0, "Forge 6 layers max " << six.max << " ms");
}
