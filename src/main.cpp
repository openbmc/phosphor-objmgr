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

const char* OBJECT_MAPPER_DBUS_NAME = "xyz.openbmc_project.ObjectMapper";

// interface_map_type is the underlying datastructure the mapper uses.
// The 3 levels of map are
// object paths
//   connection names
//      interface names
using interface_map_type = boost::container::flat_map<
    std::string, boost::container::flat_map<
                     std::string, boost::container::flat_set<std::string>>>;

/** Exception thrown when a path is not found in the object list. */
struct NotFoundException final : public sdbusplus::exception_t
{
    const char* name() const noexcept override
    {
        return "org.freedesktop.DBus.Error.FileNotFound";
    };
    const char* description() const noexcept override
    {
        return "path or object not found";
    };
    const char* what() const noexcept override
    {
        return "org.freedesktop.DBus.Error.FileNotFound: "
               "The requested object was not found";
    };
};

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

void update_owners(sdbusplus::asio::connection* conn,
                   boost::container::flat_map<std::string, std::string>& owners,
                   const std::string& new_object)
{
    if (boost::starts_with(new_object, ":"))
    {
        return;
    }
    conn->async_method_call(
        [&, new_object](const boost::system::error_code ec,
                        const std::string& nameOwner) {
            if (ec)
            {
                std::cerr << "Error getting owner of " << new_object << " : "
                          << ec << "\n";
                return;
            }
            owners[nameOwner] = new_object;
        },
        "org.freedesktop.DBus", "/", "org.freedesktop.DBus", "GetNameOwner",
        new_object);
}

void send_introspection_complete_signal(sdbusplus::asio::connection* system_bus,
                                        const std::string& process_name)
{
    // TODO(ed) This signal doesn't get exposed properly in the
    // introspect right now.  Find out how to register signals in
    // sdbusplus
    sdbusplus::message::message m = system_bus->new_signal(
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper.Private", "IntrospectionComplete");
    m.append(process_name);
    m.signal_send();
}

struct InProgressIntrospect
{
    InProgressIntrospect(
        sdbusplus::asio::connection* system_bus, boost::asio::io_service& io,
        const std::string& process_name,
        std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
            global_start_time) :
        system_bus(system_bus),
        io(io), process_name(process_name),
        global_start_time(global_start_time),
        process_start_time(std::chrono::steady_clock::now())
    {
    }
    ~InProgressIntrospect()
    {
        send_introspection_complete_signal(system_bus, process_name);
        std::chrono::duration<float> diff =
            std::chrono::steady_clock::now() - process_start_time;
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
    sdbusplus::asio::connection* system_bus;
    boost::asio::io_service& io;
    std::string process_name;

    std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
        global_start_time;
    std::chrono::time_point<std::chrono::steady_clock> process_start_time;
};

static const boost::container::flat_set<std::string> ignored_interfaces{
    "org.freedesktop.DBus.Introspectable", "org.freedesktop.DBus.Peer",
    "org.freedesktop.DBus.Properties"};

void do_getmanagedobjects(sdbusplus::asio::connection* system_bus,
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
                const std::string& path_name = object.first.str;
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
        [&interface_map, transaction, path,
         system_bus](const boost::system::error_code ec,
                     const std::string& introspect_xml) {
            if (ec)
            {
                std::cerr << "Introspect call failed with error: " << ec << ", "
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
            auto& thisPathMap = interface_map[path];
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

                if (ignored_interfaces.find(std::string(iface_name)) ==
                    ignored_interfaces.end())
                {
                    thisPathMap[transaction->process_name].emplace(iface_name);
                }
                if (std::strcmp(iface_name,
                                "xyz.openbmc_project.Associations") == 0)
                {
                    // get association
                }
                else if (std::strcmp(iface_name,
                                     "org.freedesktop.DBus.ObjectManager") == 0)
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

            if (!handling_via_objectmanager)
            {
                pElement = pRoot->FirstChildElement("node");
                while (pElement != nullptr)
                {
                    const char* child_path = pElement->Attribute("name");
                    if (child_path != nullptr)
                    {
                        std::string parent_path(path);
                        if (parent_path == "/")
                        {
                            parent_path.clear();
                        }

                        do_introspect(system_bus, transaction, interface_map,
                                      parent_path + "/" + child_path);
                    }
                    pElement = pElement->NextSiblingElement("node");
                }
            }
        },
        transaction->process_name, path, "org.freedesktop.DBus.Introspectable",
        "Introspect");
}

bool need_to_introspect(const std::string& process_name)
{
    // Don't need to introspect ourselves
    if (process_name == OBJECT_MAPPER_DBUS_NAME)
    {
        return false;
    }
    return boost::starts_with(process_name, "xyz.openbmc_project.") ||
           boost::starts_with(process_name, "org.openbmc.") ||
           boost::starts_with(process_name, "com.intel.");
}

void start_new_introspect(
    sdbusplus::asio::connection* system_bus, boost::asio::io_service& io,
    interface_map_type& interface_map, const std::string& process_name,
    std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
        global_start_time)
{
    if (need_to_introspect(process_name))
    {

        std::cerr << "starting introspect on " << process_name << "\n";
        std::shared_ptr<InProgressIntrospect> transaction =
            std::make_shared<InProgressIntrospect>(system_bus, io, process_name,
                                                   global_start_time);

        do_introspect(system_bus, transaction, interface_map, "/");
    }
}

// TODO(ed) replace with std::set_intersection once c++17 is available
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

void doListNames(
    boost::asio::io_service& io, interface_map_type& interface_map,
    sdbusplus::asio::connection* system_bus,
    boost::container::flat_map<std::string, std::string>& name_owners)
{
    system_bus->async_method_call(
        [&io, &interface_map, &name_owners,
         system_bus](const boost::system::error_code ec,
                     std::vector<std::string> process_names) {
            if (ec)
            {
                std::cerr << "Error getting names: " << ec << "\n";
                std::exit(EXIT_FAILURE);
                return;
            }
            std::cerr << "ListNames returned " << process_names.size()
                      << " entries\n";
            // Try to make startup consistent
            std::sort(process_names.begin(), process_names.end());
            std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
                global_start_time = std::make_shared<
                    std::chrono::time_point<std::chrono::steady_clock>>(
                    std::chrono::steady_clock::now());
            for (const std::string& process_name : process_names)
            {
                if (need_to_introspect(process_name))
                {
                    start_new_introspect(system_bus, io, interface_map,
                                         process_name, global_start_time);
                    update_owners(system_bus, name_owners, process_name);
                }
            }
        },
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "ListNames");
}

int main(int argc, char** argv)
{
    std::cerr << "started\n";
    boost::asio::io_service io;
    std::shared_ptr<sdbusplus::asio::connection> system_bus =
        std::make_shared<sdbusplus::asio::connection>(io);

    system_bus->request_name(OBJECT_MAPPER_DBUS_NAME);

    // Construct a signal set registered for process termination.
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&io](const boost::system::error_code& error,
                             int signal_number) { io.stop(); });

    interface_map_type interface_map;
    boost::container::flat_map<std::string, std::string> name_owners;

    std::function<void(sdbusplus::message::message & message)>
        nameChangeHandler = [&interface_map, &io, &name_owners,
                             system_bus](sdbusplus::message::message& message) {
            std::string name;
            std::string old_owner;
            std::string new_owner;

            message.read(name, old_owner, new_owner);

            if (!old_owner.empty())
            {
                if (boost::starts_with(old_owner, ":"))
                {
                    auto it = name_owners.find(old_owner);
                    if (it != name_owners.end())
                    {
                        name_owners.erase(it);
                    }
                }
                // Connection removed
                interface_map_type::iterator path_it = interface_map.begin();
                while (path_it != interface_map.end())
                {
                    path_it->second.erase(name);
                    if (path_it->second.empty())
                    {
                        // If the last connection to the object is gone,
                        // delete the top level object
                        path_it = interface_map.erase(path_it);
                        continue;
                    }
                    path_it++;
                }
            }

            if (!new_owner.empty())
            {
                auto transaction = std::make_shared<
                    std::chrono::time_point<std::chrono::steady_clock>>(
                    std::chrono::steady_clock::now());
                // New daemon added
                if (need_to_introspect(name))
                {
                    name_owners[new_owner] = name;
                    start_new_introspect(system_bus.get(), io, interface_map,
                                         name, transaction);
                }
            }
        };

    sdbusplus::bus::match::match nameOwnerChanged(
        static_cast<sdbusplus::bus::bus&>(*system_bus),
        sdbusplus::bus::match::rules::nameOwnerChanged(), nameChangeHandler);

    std::function<void(sdbusplus::message::message & message)>
        interfacesAddedHandler = [&interface_map, &name_owners](
                                     sdbusplus::message::message& message) {
            sdbusplus::message::object_path obj_path;
            std::vector<
                std::pair<std::string,
                          std::vector<std::pair<
                              std::string, sdbusplus::message::variant<bool>>>>>
                interfaces_added;
            message.read(obj_path, interfaces_added);
            std::string well_known;
            if (!get_well_known(name_owners, message.get_sender(), well_known))
            {
                return; // only introspect well-known
            }
            if (need_to_introspect(well_known))
            {
                auto& iface_list = interface_map[obj_path.str];

                for (const std::pair<
                         std::string,
                         std::vector<std::pair<
                             std::string, sdbusplus::message::variant<bool>>>>&
                         interface_pair : interfaces_added)
                {
                    iface_list[well_known].emplace(interface_pair.first);
                }
            }
        };

    sdbusplus::bus::match::match interfacesAdded(
        static_cast<sdbusplus::bus::bus&>(*system_bus),
        sdbusplus::bus::match::rules::interfacesAdded(),
        interfacesAddedHandler);

    std::function<void(sdbusplus::message::message & message)>
        interfacesRemovedHandler = [&interface_map, &name_owners](
                                       sdbusplus::message::message& message) {
            sdbusplus::message::object_path obj_path;
            std::vector<std::string> interfaces_removed;
            message.read(obj_path, interfaces_removed);
            auto connection_map = interface_map.find(obj_path.str);
            if (connection_map == interface_map.end())
            {
                return;
            }

            std::string sender;
            if (!get_well_known(name_owners, message.get_sender(), sender))
            {
                return;
            }
            for (const std::string& interface : interfaces_removed)
            {
                auto interface_set = connection_map->second.find(sender);
                if (interface_set == connection_map->second.end())
                {
                    continue;
                }

                interface_set->second.erase(interface);
                // If this was the last interface on this connection,
                // erase the connection
                if (interface_set->second.empty())
                {
                    connection_map->second.erase(interface_set);
                }
            }
            // If this was the last connection on this object path,
            // erase the object path
            if (connection_map->second.empty())
            {
                interface_map.erase(connection_map);
            }
        };

    sdbusplus::bus::match::match interfacesRemoved(
        static_cast<sdbusplus::bus::bus&>(*system_bus),
        sdbusplus::bus::match::rules::interfacesRemoved(),
        interfacesRemovedHandler);

    // Set up the object server, and send some objects
    sdbusplus::asio::object_server server(system_bus);

    std::shared_ptr<sdbusplus::asio::dbus_interface> iface =
        server.add_interface("/xyz/openbmc_project/object_mapper",
                             "xyz.openbmc_project.ObjectMapper");

    iface->register_method(
        "GetAncestors", [&interface_map](const std::string& req_path,
                                         std::vector<std::string>& interfaces) {
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
            if (ret.empty())
            {
                throw NotFoundException();
            }

            return ret;
        });

    iface->register_method(
        "GetObject", [&interface_map](const std::string& path,
                                      std::vector<std::string>& interfaces) {
            // Interfaces need to be sorted for intersect to function
            std::sort(interfaces.begin(), interfaces.end());
            auto path_ref = interface_map.find(path);
            if (path_ref == interface_map.end())
            {
                throw NotFoundException();
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
            // Unable to find intersection, return default constructed
            // object
            throw NotFoundException();
        });

    iface->register_method(
        "GetSubTree",
        [&interface_map](const std::string& req_path, int32_t depth,
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
            if (ret.empty())
            {
                throw NotFoundException();
            }
            return ret;
        });

    iface->register_method(
        "GetSubTreePaths",
        [&interface_map](const std::string& req_path, int32_t depth,
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
            if (ret.empty())
            {
                throw NotFoundException();
            }
            return ret;
        });

    iface->initialize();

    io.post([&]() {
        doListNames(io, interface_map, system_bus.get(), name_owners);
    });

    std::cerr << "starting event loop\n";
    io.run();
}
