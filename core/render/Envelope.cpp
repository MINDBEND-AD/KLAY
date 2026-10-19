// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/Envelope.h"

#include <algorithm>
#include <cmath>

namespace klay::render
{

float curveValue(float from, float to, float tension, double t) noexcept
{
    t = std::clamp(t, 0.0, 1.0);
    tension = std::clamp(tension, -1.0f, 1.0f);
    // Exponent ≥ 1 on both sides (negative tension mirrors the curve), so the slope never exceeds 8× the straight
    // line and a steep segment cannot turn into a one-sample jump (FR-285 click-free).
    const double exponent = std::pow(8.0, std::abs(static_cast<double>(tension)));
    double shaped = t;
    if (tension > 0.0f)
        shaped = std::pow(t, exponent);
    else if (tension < 0.0f)
        shaped = 1.0 - std::pow(1.0 - t, exponent);
    return static_cast<float>(from + (to - from) * shaped);
}

float envelopeValue(const model::Envelope& envelope, double timeMs) noexcept
{
    const auto& pts = envelope.points;
    if (pts.empty())
        return 1.0f;
    if (timeMs <= pts.front().timeMs)
        return pts.front().gain;
    if (timeMs >= pts.back().timeMs)
        return pts.back().gain;

    // First point strictly after timeMs; the segment starts at the point before it.
    const auto next = std::upper_bound(pts.begin(), pts.end(), timeMs,
                                       [](double t, const model::EnvelopePoint& p) { return t < p.timeMs; });
    const auto& b = *next;
    const auto& a = *(next - 1);
    const double span = b.timeMs - a.timeMs;
    if (span <= 0.0)
        return b.gain;
    return curveValue(a.gain, b.gain, a.tension, (timeMs - a.timeMs) / span);
}

void renderEnvelope(const model::Envelope& envelope, double sampleRate, size_t firstSample, float* gains,
                    size_t numSamples) noexcept
{
    const double msPerSample = 1000.0 / sampleRate;
    for (size_t i = 0; i < numSamples; ++i)
        gains[i] = envelopeValue(envelope, static_cast<double>(firstSample + i) * msPerSample);
}

model::EnvelopePoint snapToGrid(model::EnvelopePoint point, double kickLengthMs, int grid, bool freePlacement) noexcept
{
    point.timeMs = std::clamp(point.timeMs, 0.0, kickLengthMs);
    point.gain = std::clamp(point.gain, 0.0f, 1.0f);
    if (freePlacement || grid <= 0)
        return point;

    const double cellMs = kickLengthMs / grid;
    point.timeMs = std::round(point.timeMs / cellMs) * cellMs;
    point.gain = static_cast<float>(std::round(point.gain * grid) / grid);
    return point;
}

} // namespace klay::render
