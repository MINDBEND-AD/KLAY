// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/model/StateHeader.h"

#include <charconv>

namespace klay::model
{

std::string toJson(const StateHeader& header)
{
    return "{\"schemaVersion\":" + std::to_string(header.schemaVersion) + "}";
}

std::optional<StateHeader> stateHeaderFromJson(std::string_view json)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

    auto first = json.find_first_not_of(" \t\n\r");
    auto last = json.find_last_not_of(" \t\n\r");
    if (first == std::string_view::npos || json[first] != '{' || json[last] != '}')
        return std::nullopt;

    constexpr std::string_view key = "\"schemaVersion\"";
    auto pos = json.find(key);
    if (pos == std::string_view::npos)
        return std::nullopt;

    pos += key.size();
    while (pos < json.size() && isSpace(json[pos]))
        ++pos;
    if (pos >= json.size() || json[pos] != ':')
        return std::nullopt;
    ++pos;
    while (pos < json.size() && isSpace(json[pos]))
        ++pos;

    int version = 0;
    const auto* begin = json.data() + pos;
    const auto* end = json.data() + json.size();
    auto [ptr, ec] = std::from_chars(begin, end, version);
    if (ec != std::errc{} || ptr == begin || version <= 0)
        return std::nullopt;

    StateHeader header;
    header.schemaVersion = version;
    return header;
}

} // namespace klay::model
