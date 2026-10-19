// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
//
// klay-render: headless renderer for golden-file tests and preset QA (spec §10 tools/render-cli, §12).
//
//   klay-render render --kick <kick.json> --out <out.wav> [--sr 48000] [--bpm <bpm>] [--compare <golden.wav>]
//       Renders the pre-FX stack (sum of all buses, untransposed) to a 32-bit float WAV. Sample paths in the JSON
//       are relative to the JSON file. --bpm defaults to the kick's manual BPM. With --compare, exits 1 unless the
//       render nulls against the golden file within −120 dBFS (same length, max |difference| ≤ 1e-6).
//   klay-render fixtures <dir>
//       Writes the deterministic synthetic parts the fixture kicks use (plan decision 5).
#include "adapters/JuceResampler.h"
#include "adapters/SampleLoader.h"
#include "core/model/KickJson.h"
#include "core/render/StackRenderer.h"
#include "tools/render-cli/Listening.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>

namespace
{

using klay::render::StereoBuffer;

int fail(const juce::String& message)
{
    std::fprintf(stderr, "klay-render: %s\n", message.toRawUTF8());
    return 2;
}

juce::String option(const juce::StringArray& args, const juce::String& name, const juce::String& fallback = {})
{
    const int i = args.indexOf(name);
    return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : fallback;
}

StereoBuffer sumBuses(const klay::render::RenderedKick& kick)
{
    StereoBuffer sum(kick.length);
    for (const auto& bus : kick.buses)
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < kick.length; ++i)
                sum.channel(ch)[i] += bus.follow.channel(ch)[i] + bus.fixed.channel(ch)[i];
    return sum;
}

int render(const juce::StringArray& args)
{
    const juce::File kickFile = juce::File::getCurrentWorkingDirectory().getChildFile(option(args, "--kick"));
    const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile(option(args, "--out"));
    if (! kickFile.existsAsFile())
        return fail("kick JSON not found: " + kickFile.getFullPathName());

    auto kick = klay::model::kickFromJson(kickFile.loadFileAsString().toStdString());
    if (! kick)
        return fail("invalid kick JSON: " + kickFile.getFullPathName());

    const double sampleRate = option(args, "--sr", "48000").getDoubleValue();
    const double bpm = option(args, "--bpm", juce::String(kick->settings.manualBpm)).getDoubleValue();
    const auto baseDir = kickFile.getParentDirectory();

    std::map<std::string, std::shared_ptr<const klay::render::SampleData>> loaded;
    std::vector<std::string> errors;
    klay::render::RenderContext context;
    context.sampleRate = sampleRate;
    context.bpm = bpm;
    context.resamplers = klay::adapters::lagrangeResamplerFactory();
    context.samples = [&](const std::string& path) -> std::shared_ptr<const klay::render::SampleData> {
        if (const auto it = loaded.find(path); it != loaded.end())
            return it->second;
        auto result = klay::adapters::loadSample(baseDir.getChildFile(juce::String::fromUTF8(path.c_str())).getFullPathName().toStdString(), sampleRate);
        if (! result.sample)
            errors.push_back(result.error);
        return loaded[path] = result.sample;
    };

    const auto rendered = klay::render::renderKick(*kick, context);
    for (const auto& e : errors)
        std::fprintf(stderr, "klay-render: %s\n", e.c_str());
    if (! rendered.missingSamples.empty())
        return fail("missing samples, not rendering a partial kick");

    const auto sum = sumBuses(rendered);
    if (! klay::cli::writeWav(outFile, sum, sampleRate))
        return fail("cannot write " + outFile.getFullPathName());
    std::printf("rendered %s: %zu samples at %.0f Hz, %.2f BPM, autogain %.4f, key %s\n", outFile.getFileName().toRawUTF8(),
                rendered.length, sampleRate, bpm, rendered.autogain,
                rendered.key.detected ? (rendered.key.name + (rendered.key.uncertain ? "?" : "")).c_str() : "-");

    const auto compareWith = option(args, "--compare");
    if (compareWith.isEmpty())
        return 0;

    // Golden-file null test (§12): within −120 dBFS.
    const auto golden = klay::adapters::loadSample(
        juce::File::getCurrentWorkingDirectory().getChildFile(compareWith).getFullPathName().toStdString(), sampleRate);
    if (! golden.sample)
        return fail("cannot read golden file: " + golden.error);
    if (golden.sample->audio.size() != sum.size())
        return fail("length differs from the golden file: " + juce::String((int) sum.size()) + " vs "
                    + juce::String((int) golden.sample->audio.size()));
    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 0; i < sum.size(); ++i)
            maxDiff = std::max(maxDiff, (double) std::abs(sum.channel(ch)[i] - golden.sample->audio.channel(ch)[i]));
    const double diffDb = maxDiff > 0.0 ? 20.0 * std::log10(maxDiff) : -999.0;
    std::printf("null test against %s: max difference %.1f dBFS (limit -120)\n", compareWith.toRawUTF8(), diffDb);
    return diffDb <= -120.0 ? 0 : 1;
}

// Deterministic synthetic parts (plan decision 5: automated tests use synthetic kicks).
int fixtures(const juce::StringArray& args)
{
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(args[1]);
    dir.createDirectory();
    constexpr double sr = 48000.0;
    const double twoPi = 2.0 * std::numbers::pi;

    auto make = [&](const char* name, double seconds, auto&& fn) {
        StereoBuffer b(static_cast<size_t>(seconds * sr));
        double phase = 0.0;
        for (size_t i = 0; i < b.size(); ++i)
        {
            const double t = static_cast<double>(i) / sr;
            const auto [l, r] = fn(t, phase);
            b.left[i] = static_cast<float>(l);
            b.right[i] = static_cast<float>(r);
        }
        return klay::cli::writeWav(dir.getChildFile(name), b, sr, 24);
    };

    bool ok = true;
    ok &= make("body.wav", 0.6, [&](double t, double& phase) {
        phase += twoPi * (48.0 + 110.0 * std::exp(-t / 0.025)) / sr;
        const double v = 0.9 * std::sin(phase) * std::exp(-t / 0.3) * std::min(1.0, t / 0.001);
        return std::pair{ v, v };
    });
    ok &= make("crunch.wav", 0.5, [&](double t, double& phase) {
        phase += twoPi * 55.0 / sr;
        const double v = 0.7 * std::tanh(5.0 * std::sin(phase)) * std::exp(-t / 0.15) * std::min(1.0, t / 0.004);
        return std::pair{ v, 0.9 * v };
    });
    ok &= make("rumble.wav", 0.8, [&](double t, double& phase) {
        phase += twoPi * 41.0 / sr;
        const double v = 0.6 * std::sin(phase) * std::exp(-t / 0.4) * std::min(1.0, t / 0.02);
        return std::pair{ v, -v * 0.8 };
    });
    std::uint32_t seed = 0x4b4c4159u;
    ok &= make("transient.wav", 0.03, [&](double t, double&) {
        seed = seed * 1664525u + 1013904223u; // fixed LCG: same noise on every run
        const double noise = (static_cast<double>(seed >> 8) / 8388608.0) - 1.0;
        const double v = 0.8 * noise * std::exp(-t / 0.004);
        return std::pair{ v, v };
    });
    std::printf("fixture parts written to %s\n", dir.getFullPathName().toRawUTF8());
    return ok ? 0 : fail("cannot write fixture parts");
}

} // namespace

int main(int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(juce::String::fromUTF8(argv[i]));

    if (args.isEmpty())
        return fail("usage: klay-render render|fixtures|listening ... (see tools/render-cli/main.cpp)");
    if (args[0] == "render")
        return render(args);
    if (args[0] == "fixtures" && args.size() == 2)
        return fixtures(args);
    if (args[0] == "listening")
        return klay::cli::listening(args);
    return fail("unknown command: " + args[0]);
}
