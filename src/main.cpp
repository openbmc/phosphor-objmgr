#include <tinyxml2.h>
#include <atomic>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

// interface map type is the underlying datastructure the mapper uses.
// The 3 levels of map are
// object paths
//   connection names
//      interface names
using interface_map_type = boost::container::flat_map<
    std::string, boost::container::flat_map<
                     std::string, boost::container::flat_set<std::string>>>;

struct InProgressIntrospect
{
    sdbusplus::asio::connection* system_bus;
    std::string process_name;

    std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
        global_start_time;
    std::chrono::time_point<std::chrono::steady_clock> process_start_time;
    InProgressIntrospect(
        sdbusplus::asio::connection* system_bus, std::string process_name,
        std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
            global_start_time) :
        system_bus(system_bus),
        process_name(process_name), global_start_time(global_start_time),
        process_start_time(std::chrono::steady_clock::now())
    {
    }
    ~InProgressIntrospect()
    {
        // TODO(ed) This signal doesn't get exposed properly in the
        // introspect right now.  Find out how to register signals in
        // sdbusplus
        sdbusplus::message::message m =
            system_bus->new_signal("/xyz/openbmc_project/object_mapper",
                                   "xyz.openbmc_project.ObjectMapper.Private",
                                   "IntrospectionComplete");
        m.append(process_name);
        system_bus->call_noreply(m);
        std::chrono::duration<float> diff =
            std::chrono::steady_clock::now() - *global_start_time;
        std::cout << std::setw(50) << process_name << " scan took "
                  << diff.count() << " seconds\n";

        // If we're the last outstanding caller globally, calculate the
        // time it took
        if (global_start_time != nullptr && global_start_time.use_count() == 1)
        {
            diff = std::chrono::steady_clock::now() - *global_start_time;
            std::cout << "Total scan took " << diff.count()
                      << " seconds to complete\n";
        }
    }
};

struct cmp_str
{
    bool operator()(const char* a, const char* b) const
    {
        return std::strcmp(a, b) < 0;
    }
};

static const boost::container::flat_set<const char*, cmp_str>
    ignored_interfaces{"org.freedesktop.DBus.Introspectable",
                       "org.freedesktop.DBus.Peer",
                       "org.freedesktop.DBus.Properties"};

inline void
    do_getmanagedobjects(sdbusplus::asio::connection* system_bus,
                         std::shared_ptr<InProgressIntrospect> transaction,
                         interface_map_type& interface_map, std::string path)
{
    // note, the variant type doesn't matter, as we don't actually track
    // property names as of yet.  variant<bool> seemed like the most simple.
    using ManagedObjectType = std::vector<std::pair<
        sdbusplus::message::object_path,
        boost::container::flat_map<
            std::string, boost::container::flat_map<
                             std::string, sdbusplus::message::variant<bool>>>>>;
    system_bus->async_method_call(
        [&interface_map, system_bus, transaction,
         path](const boost::system::error_code ec,
               const ManagedObjectType& objects) {
            if (ec)
            {
                std::cerr << "GetMangedObjects call failed" << ec << "\n";
                return;
            }

            interface_map.reserve(interface_map.size() + objects.size());
            for (const std::pair<
                     sdbusplus::message::object_path,
                     boost::container::flat_map<
                         std::string,
                         boost::container::flat_map<
                             std::string, sdbusplus::message::variant<bool>>>>&
                     object : objects)
            {
                const std::string& path_name =
                    static_cast<const std::string&>(object.first);
                auto& this_path_map =
                    interface_map[path_name][transaction->process_name];
                for (auto& interface_it : object.second)
                {
                    this_path_map.insert(interface_it.first);
                }
            }

        },
        transaction->process_name, path, "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects");
}

void do_introspect(sdbusplus::asio::connection* system_bus,
                   std::shared_ptr<InProgressIntrospect> transaction,
                   interface_map_type& interface_map, std::string path)
{
    system_bus->async_method_call(
        [&, transaction, path, system_bus](const boost::system::error_code ec,
                                           const std::string& introspect_xml) {
            if (ec)
            {
                std::cerr << "Introspect call failed with error: "
                          << ec.message()
                          << " on process: " << transaction->process_name
                          << " path: " << path << "\n";
                return;
            }

            tinyxml2::XMLDocument doc;

            tinyxml2::XMLError e = doc.Parse(introspect_xml.c_str());
            if (e != tinyxml2::XMLError::XML_SUCCESS)
            {
                std::cerr << "XML parsing failed\n";
                return;
            }

            tinyxml2::XMLNode* pRoot = doc.FirstChildElement("node");
            if (pRoot == nullptr)
            {
                std::cerr << "XML document did not contain any data\n";
                return;
            }
            auto& this_path_map =
                interface_map[path][transaction->process_name];
            bool handling_via_objectmanager = false;
            tinyxml2::XMLElement* pElement =
                pRoot->FirstChildElement("interface");
            while (pElement != nullptr)
            {
                const char* iface_name = pElement->Attribute("name");
                if (iface_name == nullptr)
                {
                    continue;
                }

                if (ignored_interfaces.find(iface_name) ==
                    ignored_interfaces.end())
                {
                    this_path_map.emplace(iface_name);
                }
                if (std::strcmp(iface_name,
                                "xyz.openbmc_project.Associations") == 0)
                {
                    // get association
                }
                else if (std::strcmp(iface_name, "org.freedesktop.DBus."
                                                 "ObjectManager") == 0)
                {
                    // TODO(ed) in the current implementation,
                    // introspect is actually faster than
                    // getmanagedObjects, but I suspect it will be
                    // faster when needing to deal with
                    // associations, so leave the code here for now

                    // handling_via_objectmanager = true;
                    // do_getmanagedobjects(system_bus, transaction,
                    //                     interface_map, path);
                }

                pElement = pElement->NextSiblingElement("interface");
            }

            pElement = pRoot->FirstChildElement("node");
            while (pElement != nullptr)
            {
                const char* child_path = pElement->Attribute("name");
                if (child_path == nullptr)
                {
                    continue;
                }
                std::string parent_path(path);
                if (parent_path == "/")
                {
                    parent_path.clear();
                }
                if (!handling_via_objectmanager)
                {
                    do_introspect(system_bus, transaction, interface_map,
                                  parent_path + "/" + child_path);
                }

                pElement = pElement->NextSiblingElement("node");
            }

        },
        transaction->process_name, path, "org.freedesktop.DBus.Introspectable",
        "Introspect");
}

void start_new_introspect(
    sdbusplus::asio::connection* system_bus, interface_map_type& interface_map,
    const std::string& process_name,
    std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
        global_start_time)
{
    if (boost::starts_with(process_name, "xyz.openbmc_project.") ||
        boost::starts_with(process_name, "org.openbmc.") ||
        boost::starts_with(process_name, "com.intel."))
    {
        std::shared_ptr<InProgressIntrospect> transaction =
            std::make_shared<InProgressIntrospect>(system_bus, process_name,
                                                   global_start_time);
        do_introspect(system_bus, transaction, interface_map, "/");
    }
}

template <class InputIt1, class InputIt2>
bool intersect(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2)
{
    while (first1 != last1 && first2 != last2)
    {
        if (*first1 < *first2)
        {
            ++first1;
            continue;
        }
        if (*first2 < *first1)
        {
            ++first2;
            continue;
        }
        return true;
    }
    return false;
}

int main(int argc, char** argv)
{
    boost::asio::io_service io;
    auto system_bus = std::make_shared<sdbusplus::asio::connection>(io);
    system_bus->request_name("xyz.openbmc_project.ObjectMapper");

    interface_map_type interface_map;

    std::function<void(sdbusplus::message::message & message)>
        nameChangeHandler = [&](sdbusplus::message::message& message) {
            std::string name;
            std::string old_owner;
            std::string new_owner;

            message.read(name, old_owner, new_owner);

            if (!old_owner.empty())
            {
                // Connection removed
                interface_map_type::iterator path_it = interface_map.begin();
                while (path_it != interface_map.end())
                {
                    auto connection_it = path_it->second.find(old_owner);
                    if (connection_it != path_it->second.end())
                    {
                        if (connection_it->first == old_owner)
                        {
                            path_it->second.erase(connection_it);
                            break;
                        }
                    }
                    if (path_it->second.empty())
                    {
                        // If the last connection to the object is gone, delete
                        // the top level object
                        path_it = interface_map.erase(path_it);
                        continue;
                    }
                    path_it++;
                }
            }

            if (old_owner.empty() && new_owner.empty() && !name.empty())
            {
                // TODO(ed) the documentation isn't terribly clear here on
                // whether or not new_owner is allowed to be empty or if one of
                // new_owner and old_owner is required to be present
                new_owner = std::move(name);
            }

            if (!new_owner.empty())
            {
                auto transaction = std::make_shared<
                    std::chrono::time_point<std::chrono::steady_clock>>(
                    std::chrono::steady_clock::now());

                // New daemon added
                start_new_introspect(system_bus.get(), interface_map, new_owner,
                                     transaction);
            }
        };

    sdbusplus::bus::match::match nameOwnerChanged(
        *system_bus, sdbusplus::bus::match::rules::nameOwnerChanged(),
        nameChangeHandler);

    std::function<void(sdbusplus::message::message & message)>
        interfacesAddedHandler = [&](sdbusplus::message::message& message) {
            sdbusplus::message::object_path obj_path;
            std::vector<
                std::pair<std::string,
                          std::vector<std::pair<
                              std::string, sdbusplus::message::variant<bool>>>>>
                interfaces_added;
            message.read(obj_path, interfaces_added);
            const std::string& obj_str =
                static_cast<const std::string&>(obj_path);
            auto& iface_list = interface_map[obj_str];
            for (const std::pair<
                     std::string,
                     std::vector<std::pair<std::string,
                                           sdbusplus::message::variant<bool>>>>&
                     interface_pair : interfaces_added)
            {
                iface_list[interface_pair.first].emplace(message.get_sender());
            }
        };

    sdbusplus::bus::match::match interfacesAdded(
        *system_bus, sdbusplus::bus::match::rules::interfacesAdded(),
        interfacesAddedHandler);

    std::function<void(sdbusplus::message::message & message)>
        interfacesRemovedHandler = [&](sdbusplus::message::message& message) {
            sdbusplus::message::object_path obj_path;
            std::vector<std::string> interfaces_removed;
            message.read(obj_path, interfaces_removed);
            const std::string& object_path_str =
                static_cast<const std::string&>(obj_path);
            auto connection_map = interface_map.find(object_path_str);
            if (connection_map == interface_map.end())
            {
                return;
            }

            const std::string sender = std::string(message.get_sender());

            for (const std::string& interface : interfaces_removed)
            {
                auto interface_set = connection_map->second.find(sender);
                if (interface_set == connection_map->second.end())
                {
                    std::cerr << "Unable to find " << sender << " in map for "
                              << interface << "\n";
                    continue;
                }

                interface_set->second.erase(interface);
                // If this was the last interface on this connection, erase the
                // connection
                if (interface_set->second.empty())
                {
                    connection_map->second.erase(interface_set);
                }
            }
            // If this was the last connection on this object path, erase the
            // object path
            if (connection_map->second.empty())
            {
                interface_map.erase(connection_map);
            }
        };

    sdbusplus::bus::match::match interfacesRemoved(
        *system_bus, sdbusplus::bus::match::rules::interfacesRemoved(),
        interfacesRemovedHandler);

    // Set up the object server, and send some objects
    auto server = sdbusplus::asio::object_server(system_bus);

    std::shared_ptr<sdbusplus::asio::dbus_interface> iface =
        server.add_interface("/xyz/openbmc_project/object_mapper",
                             "xyz.openbmc_project.ObjectMapper");

    iface->register_method(
        "GetAncestors",
        [&](const std::string& req_path, std::vector<std::string>& interfaces) {
            // Interfaces need to be sorted for intersect to function
            std::sort(interfaces.begin(), interfaces.end());

            std::vector<interface_map_type::value_type> ret;
            for (auto& object_path : interface_map)
            {
                auto& this_path = object_path.first;
                bool add = interfaces.empty();
                if (boost::starts_with(req_path, this_path))
                {
                    for (auto& interface_map : object_path.second)
                    {
                        add = intersect(interfaces.begin(), interfaces.end(),
                                        interface_map.second.begin(),
                                        interface_map.second.end());
                        if (add)
                        {
                            break;
                        }
                    }
                    if (add)
                    {
                        ret.emplace_back(object_path);
                    }
                }
            }

            return ret;
        });

    iface->register_method(
        "GetObject",
        [&](const std::string& path, std::vector<std::string>& interfaces) {
            // Interfaces need to be sorted for intersect to function
            std::sort(interfaces.begin(), interfaces.end());
            auto path_ref = interface_map.find(path);
            if (path_ref == interface_map.end())
            {
                return decltype(path_ref->second){};
            }
            if (interfaces.empty())
            {
                return path_ref->second;
            }
            for (auto& interface_map : path_ref->second)
            {
                if (intersect(interfaces.begin(), interfaces.end(),
                              interface_map.second.begin(),
                              interface_map.second.end()))
                {
                    return path_ref->second;
                }
            }
            // Unable to find intersection, return default constructed object
            return decltype(path_ref->second){};
        });

    iface->register_method(
        "GetSubTree", [&](const std::string& req_path, int32_t depth,
                          std::vector<std::string>& interfaces) {
            if (depth <= 0)
            {
                depth = std::numeric_limits<int32_t>::max();
            }
            // Interfaces need to be sorted for intersect to function
            std::sort(interfaces.begin(), interfaces.end());
            std::vector<interface_map_type::value_type> ret;

            for (auto& object_path : interface_map)
            {
                auto& this_path = object_path.first;
                if (boost::starts_with(this_path, req_path))
                {
                    // count the number of slashes past the search term
                    int32_t this_depth =
                        std::count(this_path.begin() + req_path.size(),
                                   this_path.end(), '/');
                    if (this_depth <= depth)
                    {
                        bool add = interfaces.empty();
                        for (auto& interface_map : object_path.second)
                        {
                            if (intersect(interfaces.begin(), interfaces.end(),
                                          interface_map.second.begin(),
                                          interface_map.second.end()))
                            {
                                add = true;
                                break;
                            }
                        }
                        if (add)
                        {
                            // todo(ed) this is a copy
                            ret.emplace_back(object_path);
                        }
                    }
                }
            }
            return ret;
        });
    iface->register_method(
        "GetSubTreePaths", [&](const std::string& req_path, int32_t depth,
                               std::vector<std::string>& interfaces) {
            if (depth <= 0)
            {
                depth = std::numeric_limits<int32_t>::max();
            }
            // Interfaces need to be sorted for intersect to function
            std::sort(interfaces.begin(), interfaces.end());
            std::vector<std::string> ret;
            for (auto& object_path : interface_map)
            {
                auto& this_path = object_path.first;
                if (boost::starts_with(this_path, req_path))
                {
                    // count the number of slashes past the search term
                    int this_depth =
                        std::count(this_path.begin() + req_path.size(),
                                   this_path.end(), '/');
                    if (this_depth <= depth)
                    {
                        bool add = interfaces.empty();
                        for (auto& interface_map : object_path.second)
                        {
                            if (intersect(interfaces.begin(), interfaces.end(),
                                          interface_map.second.begin(),
                                          interface_map.second.end()))
                            {
                                add = true;
                                break;
                            }
                        }
                        if (add)
                        {
                            // TODO(ed) this is a copy
                            ret.emplace_back(this_path);
                        }
                    }
                }
            }

            return ret;
        });
    iface->initialize();

    // This needs to be done after our io_service is in run, so that the match
    // creation and name reqest happen before we start introspecting.
    io.post([&]() {
        system_bus->async_method_call(
            [&](const boost::system::error_code ec,
                const std::vector<std::string>& process_names) {
                if (ec)
                {
                    std::cerr << "error getting names: " << ec << "\n";
                }
                else
                {
                    std::shared_ptr<
                        std::chrono::time_point<std::chrono::steady_clock>>
                        global_start_time = std::make_shared<
                            std::chrono::time_point<std::chrono::steady_clock>>(
                            std::chrono::steady_clock::now());

                    for (const std::string& process_name : process_names)
                    {

                        start_new_introspect(system_bus.get(), interface_map,
                                             process_name, global_start_time);
                    }
                }
            },
            "org.freedesktop.DBus", "/", "org.freedesktop.DBus", "ListNames");
    });
    io.run();
}
