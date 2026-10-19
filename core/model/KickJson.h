// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"

#include <optional>
#include <string>
#include <string_view>

namespace klay::model
{

// Serialises a kick to human-readable, versioned JSON (spec §10, FR-003, FR-240). Output is deterministic: the same
// kick always gives the same bytes.
std::string toJson(const Kick& kick);

// Parses JSON written by toJson or by an older/newer version (NFR-012): unknown fields are ignored, missing or
// mistyped fields take their default, values are clamped to their range. Returns nullopt for malformed JSON, a
// non-object root, or a missing / non-positive schemaVersion.
std::optional<Kick> kickFromJson(std::string_view json);

} // namespace klay::model
