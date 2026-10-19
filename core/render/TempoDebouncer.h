// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include <cmath>
#include <optional>

namespace klay::render
{

// FR-002 (task 1.10): turns the host tempo seen by the audio thread into re-render requests. A change is reported
// once it has been stable for kDebounceMs, so a tempo ramp or a host sending jittery values does not trigger a
// render per block. Message thread only.
class TempoDebouncer
{
public:
    static constexpr double kDebounceMs = 150.0;
    static constexpr double kToleranceBpm = 0.005;

    // `hostBpm`: the latest host tempo (nullopt when the host sends none). Returns the tempo to render with when it
    // differs from the last one reported and has settled.
    std::optional<std::optional<double>> update(std::optional<double> hostBpm, double nowMs)
    {
        if (! same(hostBpm, candidate))
        {
            candidate = hostBpm;
            candidateSinceMs = nowMs;
        }
        if (! same(candidate, reported) && nowMs - candidateSinceMs >= kDebounceMs)
        {
            reported = candidate;
            return reported;
        }
        return std::nullopt;
    }

    std::optional<double> current() const { return reported; }

private:
    static bool same(std::optional<double> a, std::optional<double> b)
    {
        if (a.has_value() != b.has_value())
            return false;
        return ! a || std::abs(*a - *b) < kToleranceBpm;
    }

    std::optional<double> candidate, reported;
    double candidateSinceMs = 0.0;
};

} // namespace klay::render
