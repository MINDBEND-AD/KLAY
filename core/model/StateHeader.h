// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace klay::model
{

// Current version of the plugin state / preset JSON (spec §10: versioned JSON with a schemaVersion field).
inline constexpr int kSchemaVersion = 1;

// M0 state stub (FR-003): only the header of the persisted state. M1 (task 1.1/1.13) adds the stack and
// settings and replaces the hand-written JSON below with the real serialiser.
struct StateHeader
{
    int schemaVersion = kSchemaVersion;

    bool operator==(const StateHeader&) const = default;
};

// Serialises to a compact JSON object, e.g. {"schemaVersion":1}.
std::string toJson(const StateHeader& header);

// Parses the JSON written by toJson. Unknown fields are ignored. Returns nullopt for malformed input or a
// missing / non-positive schemaVersion.
std::optional<StateHeader> stateHeaderFromJson(std::string_view json);

} // namespace klay::model
