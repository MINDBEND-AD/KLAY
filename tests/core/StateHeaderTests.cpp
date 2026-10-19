// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// JUCE-free core tests (C-007): this file must not include JUCE.
#include "core/model/StateHeader.h"

#include <catch2/catch_test_macros.hpp>

using namespace klay::model;

TEST_CASE("StateHeader defaults to the current schema version", "[core][state]")
{
    StateHeader header;
    CHECK(header.schemaVersion == kSchemaVersion);
    CHECK(toJson(header) == "{\"schemaVersion\":1}");
}

TEST_CASE("StateHeader round-trips through JSON", "[core][state]")
{
    StateHeader header;
    header.schemaVersion = 7;
    auto parsed = stateHeaderFromJson(toJson(header));
    REQUIRE(parsed.has_value());
    CHECK(*parsed == header);
}

TEST_CASE("StateHeader parsing ignores unknown fields and whitespace", "[core][state]")
{
    auto parsed = stateHeaderFromJson(" {\n  \"future\": true,\n  \"schemaVersion\" : 3 }\n");
    REQUIRE(parsed.has_value());
    CHECK(parsed->schemaVersion == 3);
}

TEST_CASE("StateHeader parsing rejects malformed input", "[core][state]")
{
    CHECK_FALSE(stateHeaderFromJson("").has_value());
    CHECK_FALSE(stateHeaderFromJson("not json").has_value());
    CHECK_FALSE(stateHeaderFromJson("{}").has_value());
    CHECK_FALSE(stateHeaderFromJson("{\"schemaVersion\":\"one\"}").has_value());
    CHECK_FALSE(stateHeaderFromJson("{\"schemaVersion\":0}").has_value());
}
