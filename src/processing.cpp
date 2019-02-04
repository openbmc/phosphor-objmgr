#include <boost/algorithm/string/predicate.hpp>
#include <src/processing.hpp>

bool get_well_known(
    boost::container::flat_map<std::string, std::string>& owners,
    const std::string& request, std::string& well_known)
{
    // If it's already a well known name, just return
    if (!boost::starts_with(request, ":"))
    {
        well_known = request;
        return true;
    }

    auto it = owners.find(request);
    if (it == owners.end())
    {
        return false;
    }
    well_known = it->second;
    return true;
}

bool need_to_introspect(const std::string& process_name,
                        const WhiteBlackList& whiteList,
                        const WhiteBlackList& blackList)
{
    auto inWhitelist =
        std::find_if(whiteList.begin(), whiteList.end(),
                     [&process_name](const auto& prefix) {
                         return boost::starts_with(process_name, prefix);
                     }) != whiteList.end();

    // This holds full service names, not prefixes
    auto inBlacklist = blackList.find(process_name) != blackList.end();

    return inWhitelist && !inBlacklist;
}
