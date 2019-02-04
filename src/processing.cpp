#include <boost/algorithm/string/predicate.hpp>
#include <src/processing.hpp>

bool getWellKnown(
    const boost::container::flat_map<std::string, std::string>& owners,
    const std::string& request, std::string& wellKnown)
{
    // If it's already a well known name, just return
    if (!boost::starts_with(request, ":"))
    {
        wellKnown = request;
        return true;
    }

    auto it = owners.find(request);
    if (it == owners.end())
    {
        return false;
    }
    wellKnown = it->second;
    return true;
}

bool needToIntrospect(const std::string& processName,
                      const WhiteBlackList& whiteList,
                      const WhiteBlackList& blackList)
{
    auto inWhitelist =
        std::find_if(whiteList.begin(), whiteList.end(),
                     [&processName](const auto& prefix) {
                         return boost::starts_with(processName, prefix);
                     }) != whiteList.end();

    // This holds full service names, not prefixes
    auto inBlacklist = blackList.find(processName) != blackList.end();

    return inWhitelist && !inBlacklist;
}
