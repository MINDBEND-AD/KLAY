// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
//
// Task 2.1 on real audio: the JUCE analyser, the processor's library wiring, and the NFR-008 / NFR-007 benchmark
// (startup with a cached index < 1.5 s, RAM per instance < 300 MB). Run the benchmark alone: klay_tests "[bm]" -s
#include "adapters/SampleAnalyzer.h"
#include "plugin/PluginProcessor.h"
#include "tests/TimingGate.h"
#include "tests/plugin/LibraryFixture.h"
#include "tests/plugin/ProcessMemory.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace klay;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

TEST_CASE("Sample analyser: length, peak and root of a WAV file", "[adapters][library][FR-165][FR-120]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::SyntheticLibrary lib(1, 0.5, 48000.0, 50.0);
    const auto file = lib.dir.getChildFile("Kick").getChildFile("a").getChildFile("Kick_0.wav");
    REQUIRE(file.existsAsFile());

    const auto a = adapters::analyseSample(file.getFullPathName().toStdString());
    REQUIRE(a.ok);
    CHECK(a.lengthSeconds == Catch::Approx(0.5).margin(1e-6));
    CHECK(a.peak == Catch::Approx(0.7).margin(0.05));
    REQUIRE(a.rootDetected);
    CHECK(a.rootHz == Catch::Approx(50.0).epsilon(0.06)); // the tail settles at 50 Hz after the pitch glide
    CHECK(a.rootNote == 31);                              // G1 = 49.0 Hz

    const auto bad = lib.dir.getChildFile("bad.wav");
    bad.replaceWithText("not audio");
    CHECK_FALSE(adapters::analyseSample(bad.getFullPathName().toStdString()).ok);
    CHECK_FALSE(adapters::analyseSample(lib.dir.getChildFile("none.wav").getFullPathName().toStdString()).ok);
}

TEST_CASE("Processor indexes its library folder and keeps the cache in the data folder", "[plugin][library][FR-160][FR-165]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::SyntheticLibrary lib(2);
    {
        KlayProcessor processor(ProcessorOptions{ lib.root(), lib.dataDir() });
        REQUIRE(processor.getLibrary().waitUntilReady(30s));
        const auto index = processor.getLibrary().snapshot();
        CHECK(index->samples.size() == 12);
        CHECK(processor.getLibrary().lastStats().analysed == 12);
        CHECK(std::filesystem::exists(lib.dataDir() / "library-index.json"));
    }
    {
        // setLibraryRoot persists the folder; a processor without an explicit folder then picks it up.
        KlayProcessor processor(ProcessorOptions{ std::filesystem::path(), lib.dataDir() });
        CHECK(processor.getLibrary().status() == library::LibraryScanner::Status::idle);
        processor.setLibraryRoot(lib.root());
        REQUIRE(processor.getLibrary().waitUntilReady(30s));
        CHECK(processor.getLibrary().loadedFromCache());
    }
    {
        KlayProcessor processor(ProcessorOptions{ std::nullopt, lib.dataDir() });
        CHECK(processor.getLibraryRoot() == lib.root());
        REQUIRE(processor.getLibrary().waitUntilReady(30s));
        CHECK(processor.getLibrary().lastStats().reused == 12);
    }
}

// NFR-008: a new instance with the index cached is ready (index published and checked against the folder) within
// 1.5 s; NFR-007: the instance holds no library audio, RAM stays far below 300 MB.
TEST_CASE("NFR-008 / NFR-007: startup with a cached index and RAM per instance", "[bm][NFR-008][NFR-007]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr int perRole = 100; // 600 files, about 7× the current factory library
    test::SyntheticLibrary lib(perRole);

    {
        const auto t0 = Clock::now();
        KlayProcessor first(ProcessorOptions{ lib.root(), lib.dataDir() });
        REQUIRE(first.getLibrary().waitUntilReady(120s));
        const double scanMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        WARN("NFR-008 first scan (no cache) of " << perRole * 6 << " files: " << scanMs << " ms");
    }

    const auto memBefore = test::privateBytes();
    const auto t0 = Clock::now();
    KlayProcessor processor(ProcessorOptions{ lib.root(), lib.dataDir() });
    const auto constructed = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    REQUIRE(processor.getLibrary().waitUntilReady(30s));
    const auto ready = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    const auto memAfter = test::privateBytes();
    const double mb = (static_cast<double>(memAfter) - static_cast<double>(memBefore)) / (1024.0 * 1024.0);

    CHECK(processor.getLibrary().loadedFromCache());
    CHECK(processor.getLibrary().lastStats().reused == perRole * 6);
    CHECK(processor.getLibrary().lastStats().analysed == 0);
    WARN("NFR-008 startup with cached index (" << perRole * 6 << " files): constructor " << constructed
                                               << " ms, index ready " << ready << " ms (target < 1500 ms)");
    WARN("NFR-007 RAM of the instance after startup: " << mb << " MB private bytes (target < 300 MB)");
    KLAY_TIMING_CHECK(ready < 1500.0, "startup " << ready << " ms");
    CHECK(mb < 300.0);
}
