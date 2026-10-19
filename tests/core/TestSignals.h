// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

// Synthetic test material (plan decision 5: automated tests use synthetic kicks) and measurement helpers. JUCE-free.

#include "core/dsp/Fft.h"
#include "core/render/StereoBuffer.h"

#include <cmath>
#include <numbers>

namespace klay::test
{

inline render::StereoBuffer sine(double hz, double sampleRate, size_t n, float amplitude = 0.5f)
{
    render::StereoBuffer b(n);
    for (size_t i = 0; i < n; ++i)
        b.left[i] = b.right[i] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sampleRate));
    return b;
}

// Frequency from the positive-going zero crossings in [from, to), interpolated linearly: exact to well under a cent
// for a clean tone over many cycles.
inline double zeroCrossingHz(const float* x, size_t from, size_t to, double sampleRate)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double pos = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0.0)
                first = pos;
            last = pos;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * sampleRate / (last - first) : 0.0;
}

inline double cents(double hz, double referenceHz)
{
    return 1200.0 * std::log2(hz / referenceHz);
}

inline double rms(const float* x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return to > from ? std::sqrt(sum / static_cast<double>(to - from)) : 0.0;
}

inline double fftPeakHz(const float* x, size_t n, double sampleRate, double minHz, double maxHz)
{
    size_t fftSize = 1;
    while (fftSize < n * 4)
        fftSize <<= 1;
    return dsp::peakFrequency(dsp::magnitudeSpectrum(x, n, fftSize), fftSize, sampleRate, minHz, maxHz);
}

} // namespace klay::test
