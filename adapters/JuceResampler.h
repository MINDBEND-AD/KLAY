// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/render/Resampler.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <memory>

namespace klay::adapters
{

// Repitch resampler (FR-106, spec §10): JUCE LagrangeInterpolator behind the core Resampler interface (C-007).
// Real-time safe: no allocation in reset() or process().
template <typename Interpolator>
class JuceResampler final : public render::Resampler
{
public:
    void reset() noexcept override { interpolator.reset(); }

    int latencySamples() const noexcept override
    {
        return static_cast<int>(std::ceil(Interpolator::getBaseLatency()));
    }

    int process(double speedRatio, const float* in, float* out, int numOut) noexcept override
    {
        return interpolator.process(speedRatio, in, out, numOut);
    }

private:
    Interpolator interpolator;
};

using LagrangeResampler = JuceResampler<juce::LagrangeInterpolator>;

inline render::ResamplerFactory lagrangeResamplerFactory()
{
    return [] { return std::make_unique<LagrangeResampler>(); };
}

} // namespace klay::adapters
