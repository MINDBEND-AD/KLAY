// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace klay::dsp
{

void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

std::vector<double> magnitudeSpectrum(const float* signal, size_t numSamples, size_t fftSize)
{
    numSamples = std::min(numSamples, fftSize);
    std::vector<std::complex<double>> data(fftSize);
    for (size_t i = 0; i < numSamples; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(numSamples));
        data[i] = signal[i] * w;
    }
    fft(data);
    std::vector<double> mags(fftSize / 2 + 1);
    for (size_t i = 0; i < mags.size(); ++i)
        mags[i] = std::abs(data[i]);
    return mags;
}

double peakFrequency(const std::vector<double>& mags, size_t fftSize, double sampleRate, double minHz, double maxHz)
{
    const double binHz = sampleRate / static_cast<double>(fftSize);
    const size_t lo = std::max<size_t>(1, static_cast<size_t>(std::floor(minHz / binHz)));
    const size_t hi = std::min(mags.size() - 2, static_cast<size_t>(std::ceil(maxHz / binHz)));
    if (lo > hi)
        return 0.0;

    size_t best = lo;
    for (size_t i = lo; i <= hi; ++i)
        if (mags[i] > mags[best])
            best = i;
    if (mags[best] <= 0.0)
        return 0.0;

    const double a = std::log(mags[best - 1] + 1e-30);
    const double b = std::log(mags[best] + 1e-30);
    const double c = std::log(mags[best + 1] + 1e-30);
    const double denom = a - 2.0 * b + c;
    const double delta = denom != 0.0 ? std::clamp(0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;
    return (static_cast<double>(best) + delta) * binHz;
}

} // namespace klay::dsp
