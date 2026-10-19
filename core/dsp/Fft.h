// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include <complex>
#include <vector>

namespace klay::dsp
{

// In-place radix-2 complex FFT (forward, unnormalised). The size must be a power of two. Used offline only (key
// detection, tests), so it favours simplicity over speed.
void fft(std::vector<std::complex<double>>& data);

// Magnitude spectrum of a Hann-windowed real signal, zero-padded to fftSize (a power of two): fftSize / 2 + 1 bins.
std::vector<double> magnitudeSpectrum(const float* signal, size_t numSamples, size_t fftSize);

// Frequency of the strongest bin in [minHz, maxHz], refined by parabolic interpolation on log magnitudes.
// Returns 0 if the range holds no energy.
double peakFrequency(const std::vector<double>& magnitudes, size_t fftSize, double sampleRate, double minHz, double maxHz);

} // namespace klay::dsp
