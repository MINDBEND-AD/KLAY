// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// Sample index (task 2.1: FR-160, FR-165) on a fixture tree written by the test. The analyser is a fake that counts
// its calls, so these tests prove what is analysed, reused and invalidated without decoding audio.
#include "core/library/LibraryScanner.h"
#include "core/library/SampleIndex.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <random>
#include <set>
#include <thread>

using namespace klay;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace
{

struct FixtureTree
{
    fs::path root;

    FixtureTree()
    {
        std::random_device rd;
        root = fs::temp_directory_path() / ("klay-library-" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(root);
    }
    ~FixtureTree()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path write(const fs::path& relative, size_t bytes = 100)
    {
        const auto path = root / relative;
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << std::string(bytes, 'x');
        return path;
    }
};

// Kick/punchy/a.wav, Kick/b.wav (directly in the role folder), RUMBLE/deep/long/c.aiff (case-insensitive role,
// nested sub-folder), Crunch/d.flac, Texture/e.aif; ignored: a .txt, a non-role folder, a file at the root.
void writeStandardTree(FixtureTree& t)
{
    t.write("Kick/punchy/a.wav");
    t.write("Kick/b.wav", 200);
    t.write("RUMBLE/deep/long/c.aiff");
    t.write("Crunch/d.flac");
    t.write("Texture/e.aif");
    t.write("Kick/notes.txt");
    t.write("Loops/f.wav");
    t.write("g.wav");
}

struct CountingAnalyzer
{
    std::shared_ptr<std::atomic<int>> calls = std::make_shared<std::atomic<int>>(0);

    library::Analyzer get() const
    {
        return [calls = calls](const std::string& path) {
            calls->fetch_add(1);
            library::Analysis a;
            if (path.find("corrupt") != std::string::npos)
            {
                a.error = "corrupt";
                return a;
            }
            a.ok = true;
            a.lengthSeconds = 0.5;
            a.peak = 0.8f;
            a.rootDetected = true;
            a.rootNote = 29;
            a.rootHz = 43.65;
            return a;
        };
    }
};

const library::SampleInfo* find(const library::SampleIndex& index, const std::string& fileName)
{
    for (const auto& s : index.samples)
        if (library::fromUtf8(s.path).filename() == fileName)
            return &s;
    return nullptr;
}

} // namespace

TEST_CASE("Library scan: Role -> sub-folder -> files", "[core][library][FR-160][FR-161]")
{
    FixtureTree t;
    writeStandardTree(t);
    const auto found = library::scanLibrary(t.root);
    REQUIRE(found.size() == 5);

    std::set<std::string> names;
    for (const auto& f : found)
        names.insert(library::toUtf8(f.path.filename()));
    CHECK(names == std::set<std::string>{ "a.wav", "b.wav", "c.aiff", "d.flac", "e.aif" });

    CountingAnalyzer analyzer;
    const auto index = library::buildIndex(t.root, analyzer.get(), nullptr);
    REQUIRE(index.samples.size() == 5);
    CHECK(find(index, "a.wav")->role == model::Role::kick);
    CHECK(find(index, "a.wav")->folder == "punchy");
    CHECK(find(index, "b.wav")->folder.empty());
    CHECK(find(index, "c.aiff")->role == model::Role::rumble);
    CHECK(find(index, "c.aiff")->folder == "deep/long");
    CHECK(find(index, "d.flac")->role == model::Role::crunch);
    CHECK(find(index, "e.aif")->role == model::Role::texture);
    // FR-165 metadata from the analyser, file stamp from the file system.
    CHECK(find(index, "b.wav")->size == 200);
    CHECK(find(index, "b.wav")->meta.rootNote == 29);
    CHECK(find(index, "b.wav")->meta.peak == 0.8f);
    CHECK(find(index, "b.wav")->meta.lengthSeconds == 0.5);
    // Sorted by path, so pools are deterministic (FR-147).
    for (size_t i = 1; i < index.samples.size(); ++i)
        CHECK(library::fromUtf8(index.samples[i - 1].path) < library::fromUtf8(index.samples[i].path));

    CHECK(library::scanLibrary(t.root / "missing").empty());
    CHECK(library::roleFromFolderName("transient") == model::Role::transient);
    CHECK_FALSE(library::roleFromFolderName("Any").has_value()); // Any is a layer role, not a folder
}

TEST_CASE("Library index: built, reused from the disk cache, invalidated by mtime and size", "[core][library][FR-165][NFR-008]")
{
    FixtureTree t;
    writeStandardTree(t);
    CountingAnalyzer analyzer;
    const auto cacheFile = t.root / "cache" / "library-index.json";

    // Built: every file analysed once.
    library::BuildStats stats;
    const auto first = library::buildIndex(t.root, analyzer.get(), nullptr, &stats);
    CHECK(stats.analysed == 5);
    CHECK(stats.reused == 0);
    CHECK(analyzer.calls->load() == 5);
    REQUIRE(library::saveIndex(cacheFile, first));

    // Cache round-trip is exact.
    const auto cached = library::loadIndex(cacheFile);
    REQUIRE(cached);
    CHECK(cached->root == first.root);
    CHECK(cached->samples == first.samples);

    // Reused: nothing changed, nothing analysed.
    const auto second = library::buildIndex(t.root, analyzer.get(), &*cached, &stats);
    CHECK(stats.analysed == 0);
    CHECK(stats.reused == 5);
    CHECK(analyzer.calls->load() == 5);
    CHECK(second.samples == first.samples);

    // Invalidated: a newer mtime, a changed size, a removed and an added file.
    const auto a = t.root / "Kick/punchy/a.wav";
    fs::last_write_time(a, fs::last_write_time(a) + 1h);
    t.write("Crunch/d.flac", 300);
    fs::remove(t.root / "Texture/e.aif");
    t.write("Transient/clicks/h.wav");
    const auto third = library::buildIndex(t.root, analyzer.get(), &second, &stats);
    CHECK(stats.analysed == 3); // a (mtime), d (size), h (new)
    CHECK(stats.reused == 2);   // b, c
    CHECK(stats.removed == 1);  // e
    CHECK(analyzer.calls->load() == 8);
    CHECK(third.samples.size() == 5);
    CHECK(find(third, "e.aif") == nullptr);
    CHECK(find(third, "h.wav")->role == model::Role::transient);
    CHECK(find(third, "d.flac")->size == 300);

    // A cache from another library folder is not used.
    auto other = second;
    other.root = library::toUtf8(t.root / "elsewhere");
    library::buildIndex(t.root, analyzer.get(), &other, &stats);
    CHECK(stats.reused == 0);
}

TEST_CASE("Library index: unreadable files are left out and reported", "[core][library][FR-165]")
{
    FixtureTree t;
    t.write("Kick/good.wav");
    t.write("Kick/corrupt.wav");
    CountingAnalyzer analyzer;
    library::BuildStats stats;
    const auto index = library::buildIndex(t.root, analyzer.get(), nullptr, &stats);
    CHECK(index.samples.size() == 1);
    CHECK(stats.failed == 1);
    CHECK(stats.analysed == 1);
}

TEST_CASE("Library cache: malformed or other-version files are ignored", "[core][library][FR-165][NFR-012]")
{
    CHECK_FALSE(library::indexFromJson("not json").has_value());
    CHECK_FALSE(library::indexFromJson("[]").has_value());
    CHECK_FALSE(library::indexFromJson(R"({"indexVersion": 99, "root": "x", "samples": []})").has_value());
    CHECK_FALSE(library::indexFromJson(R"({"indexVersion": 1, "root": "x", "samples": [{"path": 3}]})").has_value());
    CHECK(library::indexFromJson(R"({"indexVersion": 1, "root": "x", "samples": []})").has_value());
    CHECK_FALSE(library::loadIndex(fs::temp_directory_path() / "klay-no-such-index.json").has_value());
}

TEST_CASE("Library scanner: publishes the cached index first, then the checked one", "[core][library][FR-165][NFR-008]")
{
    FixtureTree t;
    writeStandardTree(t);
    CountingAnalyzer analyzer;
    const auto cacheFile = t.root / "cache.json";

    library::LibraryScanner scanner;
    CHECK(scanner.status() == library::LibraryScanner::Status::idle);
    CHECK(scanner.snapshot()->samples.empty());

    // First start: no cache, everything analysed, cache written.
    scanner.start({ t.root, cacheFile, analyzer.get(), 2 });
    REQUIRE(scanner.waitUntilReady(10s));
    CHECK(scanner.status() == library::LibraryScanner::Status::ready);
    CHECK_FALSE(scanner.loadedFromCache());
    CHECK(scanner.snapshot()->samples.size() == 5);
    CHECK(scanner.lastStats().analysed == 5);
    CHECK(fs::exists(cacheFile));

    // A new instance: starts from the cache, analyses nothing.
    library::LibraryScanner second;
    second.start({ t.root, cacheFile, analyzer.get(), 2 });
    REQUIRE(second.waitUntilReady(10s));
    CHECK(second.loadedFromCache());
    CHECK(second.lastStats().reused == 5);
    CHECK(second.lastStats().analysed == 0);
    CHECK(analyzer.calls->load() == 5);
    CHECK(second.snapshot()->samples == scanner.snapshot()->samples);

    // No folder: idle, empty.
    second.start({});
    CHECK(second.status() == library::LibraryScanner::Status::idle);
    CHECK(second.snapshot()->samples.empty());
}

TEST_CASE("Library scanner: stop cancels a running scan promptly", "[core][library][FR-165]")
{
    FixtureTree t;
    for (int i = 0; i < 200; ++i)
        t.write("Kick/k" + std::to_string(i) + ".wav");
    std::atomic<int> calls{ 0 };
    library::LibraryScanner scanner;
    scanner.start({ t.root, {}, [&calls](const std::string&) {
                       calls.fetch_add(1);
                       std::this_thread::sleep_for(5ms);
                       library::Analysis a;
                       a.ok = true;
                       return a;
                   },
                    1 });
    std::this_thread::sleep_for(20ms);
    const auto t0 = std::chrono::steady_clock::now();
    scanner.stop();
    CHECK(std::chrono::steady_clock::now() - t0 < 500ms);
    CHECK(calls.load() < 200);
}
