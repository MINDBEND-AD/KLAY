// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "tools/render-cli/Listening.h"

#include "adapters/JuceResampler.h"
#include "adapters/SampleLoader.h"
#include "core/render/Stretcher.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>
#include <random>

namespace klay::cli
{

bool writeWav(const juce::File& file, const render::StereoBuffer& audio, double sampleRate, int bits)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::AudioBuffer<float> buffer(2, static_cast<int>(audio.size()));
    for (int ch = 0; ch < 2; ++ch)
        buffer.copyFrom(ch, 0, audio.channel(ch), static_cast<int>(audio.size()));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(bits);
    if (bits == 32)
        options = options.withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor(stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

namespace
{
    using render::StereoBuffer;

    double rms(const StereoBuffer& b)
    {
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < b.size(); ++i)
                sum += static_cast<double>(b.channel(ch)[i]) * b.channel(ch)[i];
        return b.size() ? std::sqrt(sum / static_cast<double>(2 * b.size())) : 0.0;
    }

    // LT-1 step 4: level-matched comparisons. Every file is scaled to the original part's RMS.
    StereoBuffer matched(StereoBuffer b, double targetRms)
    {
        const double r = rms(b);
        if (r > 0.0)
            for (int ch = 0; ch < 2; ++ch)
                for (size_t i = 0; i < b.size(); ++i)
                    b.channel(ch)[i] *= static_cast<float>(targetRms / r);
        return b;
    }

    juce::String ratioName(double r)
    {
        return juce::String(r, r == std::floor(r) ? 0 : 2) + "x";
    }

    juce::String stName(int st)
    {
        return (st > 0 ? "+" : "") + juce::String(st) + "st";
    }

    struct Part
    {
        juce::String role, name;
        StereoBuffer audio;
    };

    // Blind A/B: copies the two files under neutral names in a fixed-seed random order and records the key.
    void blindPair(const juce::File& dir, const juce::String& id, const juce::File& a, const juce::File& b, std::mt19937& rng,
                   juce::String& key)
    {
        dir.createDirectory();
        const bool swap = (rng() & 1u) != 0;
        const bool copied = a.copyFileTo(dir.getChildFile(id + "_" + (swap ? "B" : "A") + ".wav"))
                            && b.copyFileTo(dir.getChildFile(id + "_" + (swap ? "A" : "B") + ".wav"));
        if (! copied)
            std::fprintf(stderr, "klay-render: cannot write blind pair %s\n", id.toRawUTF8());
        key << id << "," << (swap ? b.getFileName() : a.getFileName()) << "," << (swap ? a.getFileName() : b.getFileName()) << "\n";
    }
} // namespace

// klay-render listening --samples <dir> --out <dir> [--sr 48000]
// Parts are read from <dir>/<Role>/*.wav|aif|aiff|flac (the staging folder: one sub-folder per layer role) or from
// <dir>/*.wav (role "Any"). LT-1 uses every part; LT-2 and AC-5 use the pitched roles (Kick, Rumble, Crunch, Any).
int listening(const juce::StringArray& args)
{
    auto option = [&](const juce::String& name, const juce::String& fallback = {}) {
        const int i = args.indexOf(name);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : fallback;
    };
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto samplesDir = cwd.getChildFile(option("--samples"));
    const auto outDir = cwd.getChildFile(option("--out"));
    const double sr = option("--sr", "48000").getDoubleValue();
    if (! samplesDir.isDirectory() || option("--out").isEmpty())
    {
        std::fprintf(stderr, "klay-render: usage: listening --samples <dir> --out <dir> [--sr 48000]\n");
        return 2;
    }

    std::vector<Part> parts;
    const juce::String patterns = "*.wav;*.aif;*.aiff;*.flac";
    auto addFrom = [&](const juce::File& dir, const juce::String& role) {
        auto files = dir.findChildFiles(juce::File::findFiles, false, patterns);
        files.sort();
        for (const auto& f : files)
        {
            auto loaded = adapters::loadSample(f.getFullPathName().toStdString(), sr);
            if (! loaded.sample)
            {
                std::fprintf(stderr, "klay-render: skipped %s\n", loaded.error.c_str());
                continue;
            }
            parts.push_back({ role, f.getFileNameWithoutExtension(), loaded.sample->audio });
        }
    };
    addFrom(samplesDir, "Any");
    auto roleDirs = samplesDir.findChildFiles(juce::File::findDirectories, false);
    roleDirs.sort();
    for (const auto& d : roleDirs)
        addFrom(d, d.getFileName());
    if (parts.empty())
    {
        std::fprintf(stderr, "klay-render: no parts found under %s\n", samplesDir.getFullPathName().toRawUTF8());
        return 2;
    }

    const auto resamplers = adapters::lagrangeResamplerFactory();
    std::mt19937 rng(0x4b4c4159u); // fixed seed: the same material and blind order on every run
    juce::String lt1Key = "id,A,B\n", ac5Key = "id,A,B\n";
    int files = 0;

    for (const auto& part : parts)
    {
        const double reference = rms(part.audio);
        const auto base = part.role + "/" + part.name;

        // LT-1: 5 ratios × 2 presets + unprocessed and Repitch references.
        const auto lt1 = outDir.getChildFile("LT-1").getChildFile(base);
        writeWav(lt1.getChildFile("ref_original.wav"), part.audio, sr, 24);
        ++files;
        for (double ratio : { 0.5, 0.75, 1.5, 2.0, 4.0 })
        {
            const auto rName = ratioName(ratio);
            const auto def = lt1.getChildFile("stretch_default_" + rName + ".wav");
            const auto cheap = lt1.getChildFile("stretch_cheaper_" + rName + ".wav");
            writeWav(def, matched(render::stretch(part.audio, sr, ratio, 0.0, model::StretchPreset::standard), reference), sr, 24);
            writeWav(cheap, matched(render::stretch(part.audio, sr, ratio, 0.0, model::StretchPreset::cheaper), reference), sr, 24);
            writeWav(lt1.getChildFile("ref_repitch_" + rName + ".wav"),
                     matched(render::repitch(part.audio, -12.0 * std::log2(ratio), resamplers), reference), sr, 24);
            files += 3;
            blindPair(outDir.getChildFile("LT-1").getChildFile("blind"), part.role + "_" + part.name + "_" + rName, def, cheap,
                      rng, lt1Key);
        }

        if (part.role != "Kick" && part.role != "Rumble" && part.role != "Crunch" && part.role != "Any")
            continue;

        // LT-2: transpositions as Key Track plays them from the cache (length kept).
        const auto lt2 = outDir.getChildFile("LT-2").getChildFile(base);
        for (int st : { -24, -12, -5, 5, 12, 24 })
        {
            writeWav(lt2.getChildFile(stName(st) + ".wav"),
                     matched(render::stretch(part.audio, sr, 1.0, st, model::StretchPreset::standard, resamplers), reference), sr, 24);
            ++files;
        }

        // AC-5: the first hit (real-time Repitch fallback) against the cached hit, blind.
        const auto ac5 = outDir.getChildFile("AC-5").getChildFile(base);
        for (int st : { -12, -5, -1, 1, 5, 12 })
        {
            const auto cached = ac5.getChildFile(stName(st) + "_cached.wav");
            const auto fallback = ac5.getChildFile(stName(st) + "_fallback.wav");
            writeWav(cached, matched(render::stretch(part.audio, sr, 1.0, st, model::StretchPreset::standard, resamplers), reference), sr, 24);
            writeWav(fallback, matched(render::repitch(part.audio, st, resamplers), reference), sr, 24);
            files += 2;
            blindPair(outDir.getChildFile("AC-5").getChildFile("blind"), part.role + "_" + part.name + "_" + stName(st), cached,
                      fallback, rng, ac5Key);
        }
    }

    outDir.getChildFile("LT-1").getChildFile("blind").getChildFile("key.csv").replaceWithText(lt1Key);
    outDir.getChildFile("AC-5").getChildFile("blind").getChildFile("key.csv").replaceWithText(ac5Key);
    std::printf("listening material: %d parts, %d files in %s\n", (int) parts.size(), files, outDir.getFullPathName().toRawUTF8());
    return 0;
}

} // namespace klay::cli
