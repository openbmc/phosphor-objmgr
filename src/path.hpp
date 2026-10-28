#pragma once

#include <algorithm>
#include <string>
#include <string_view>

// Whether or not name can be used as an object path element. An object path
// element must be non-empty and may only contain the characters [A-Z][a-z]
// [0-9]_.
inline bool isValidPathElement(std::string_view name)
{
    return !name.empty() && std::ranges::all_of(name, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '_';
    });
}

inline std::string appendPathSegment(const std::string& target,
                                     const std::string& segment)
{
    std::string newPath;
    newPath.reserve(target.size() + 1 + segment.size());
    newPath.append(target).append("/").append(segment);
    return newPath;
}
