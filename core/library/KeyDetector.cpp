// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/library/KeyDetector.h"
#include "core/dsp/Fft.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace klay::library
{

double midiToHz(double midiNote) noexcept
{
    return 440.0 * std::exp2((midiNote - 69.0) / 12.0);
}

double hzToMidi(double hz) noexcept
{
    return 69.0 + 12.0 * std::log2(hz / 440.0);
}

std::string noteName(int midiNote)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = (midiNote / 12) - 1;
    return std::string(names[((midiNote % 12) + 12) % 12]) + std::to_string(octave);
}

namespace
{
    // YIN (de Cheveigné & Kawahara 2002) on one frame: cumulative mean normalised difference, absolute threshold,
    // parabolic refinement. Returns 0 for an unvoiced frame. The difference function is computed lag by lag and stops
    // one lag past the chosen dip (the cumulative normalisation needs no later lags), so a typical kick frame skips
    // about a third of the work with the same result (NFR-005).
    double yinFrame(const float* x, size_t window, size_t minTau, size_t maxTau, double sampleRate, std::vector<double>& d)
    {
        constexpr double threshold = 0.15;
        d.assign(maxTau + 2, 0.0);
        d[0] = 1.0;
        double running = 0.0;
        size_t best = 0; // first lag ≥ minTau below the threshold, then walked down to the local minimum
        for (size_t tau = 1; tau <= maxTau + 1; ++tau)
        {
            double sum = 0.0;
            for (size_t i = 0; i < window; ++i)
            {
                const double diff = static_cast<double>(x[i]) - x[i + tau];
                sum += diff * diff;
            }
            running += sum;
            d[tau] = running > 0.0 ? sum * static_cast<double>(tau) / running : 1.0;

            if (best != 0)
            {
                if (tau <= maxTau && d[tau] < d[best])
                    best = tau; // still descending
                else
                    break; // d[best + 1] is known: done
            }
            else if (tau >= minTau && tau <= maxTau && d[tau] < threshold)
                best = tau;
        }
        if (best == 0)
            return 0.0;

        const double a = d[best - 1], b = d[best], c = d[best + 1];
        const double denom = a - 2.0 * b + c;
        const double shift = denom != 0.0 ? std::clamp(0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;
        return sampleRate / (static_cast<double>(best) + shift);
    }

    double median(std::vector<double> v)
    {
        std::sort(v.begin(), v.end());
        const size_t m = v.size() / 2;
        return v.size() % 2 ? v[m] : 0.5 * (v[m - 1] + v[m]);
    }
} // namespace

render::KeyInfo detectKey(const float* mono, size_t n, double sampleRate, bool parallel)
{
    render::KeyInfo info;
    const auto minTau = static_cast<size_t>(std::floor(sampleRate / kKeyMaxHz));
    const auto maxTau = static_cast<size_t>(std::ceil(sampleRate / kKeyMinHz));
    const size_t window = maxTau; // one period of the lowest frequency
    const size_t hop = window / 4;
    if (n < window + maxTau + 2)
        return info;

    // Frame energies, to find the attack and skip near-silent frames.
    struct Frame { size_t start; double rms; };
    std::vector<Frame> frames;
    double maxRms = 0.0;
    for (size_t start = 0; start + window + maxTau + 2 <= n; start += hop)
    {
        double sum = 0.0;
        for (size_t i = 0; i < window; ++i)
            sum += static_cast<double>(mono[start + i]) * mono[start + i];
        frames.push_back({ start, std::sqrt(sum / static_cast<double>(window)) });
        maxRms = std::max(maxRms, frames.back().rms);
    }
    if (maxRms <= 0.0)
        return info;

    const auto peakFrame = static_cast<size_t>(std::max_element(frames.begin(), frames.end(),
                                                                [](const Frame& a, const Frame& b) { return a.rms < b.rms; })
                                               - frames.begin());

    // Frames from the peak until the tail drops below about −30 dB of it. Each frame is independent, so they run on
    // several threads (NFR-005: the key layer's detection is on the Forge-to-audible path) and are gathered in frame
    // order: the result does not depend on the thread count.
    size_t endFrame = peakFrame;
    while (endFrame < frames.size() && frames[endFrame].rms >= maxRms * 0.03)
        ++endFrame;
    std::vector<double> f0(endFrame - peakFrame, 0.0);
    std::atomic<size_t> next{ peakFrame };
    auto work = [&] {
        std::vector<double> d;
        for (size_t f = next.fetch_add(1); f < endFrame; f = next.fetch_add(1))
            f0[f - peakFrame] = yinFrame(mono + frames[f].start, window, minTau, maxTau, sampleRate, d);
    };
    const size_t threads = parallel ? std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), (f0.size() + 7) / 8) : 1;
    std::vector<std::thread> pool;
    for (size_t t = 1; t < threads; ++t)
        pool.emplace_back(work);
    work();
    for (auto& t : pool)
        t.join();

    std::vector<std::pair<size_t, double>> voiced; // frame start, f0
    for (size_t f = peakFrame; f < endFrame; ++f)
        if (const double hz = f0[f - peakFrame]; hz >= kKeyMinHz && hz <= kKeyMaxHz)
            voiced.emplace_back(frames[f].start, hz);
    if (voiced.empty())
        return info;

    // Settled tail: drop the first 40 % of the voiced frames (attack and pitch glide).
    const size_t first = voiced.size() * 2 / 5;
    std::vector<double> settled;
    for (size_t i = first; i < voiced.size(); ++i)
        settled.push_back(voiced[i].second);
    info.hz = median(settled);
    info.detected = true;

    // FFT cross-check over the same region.
    const size_t regionStart = voiced[first].first;
    const size_t regionEnd = std::min(n, voiced.back().first + window);
    const size_t regionLength = regionEnd - regionStart;
    size_t fftSize = 1;
    while (fftSize < std::max<size_t>(regionLength * 4, 65536))
        fftSize <<= 1;
    info.fftHz = dsp::peakFrequency(dsp::magnitudeSpectrum(mono + regionStart, regionLength, fftSize), fftSize,
                                    sampleRate, kKeyMinHz, kKeyMaxHz);
    info.uncertain = info.fftHz <= 0.0 || std::abs(1200.0 * std::log2(info.fftHz / info.hz)) > kUncertainCents;

    const double midi = hzToMidi(info.hz);
    info.midiNote = static_cast<int>(std::lround(midi));
    info.centsOff = (midi - info.midiNote) * 100.0;
    info.name = noteName(info.midiNote);
    return info;
}

} // namespace klay::library
