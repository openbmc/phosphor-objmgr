#include <string>

inline std::string appendPathSegment(const std::string& target,
                                     const std::string& segment)
{
    std::string newPath;
    newPath.reserve(target.size() + 1 + segment.size());
    newPath.append(target).append("/").append(segment);
    return newPath;
}
