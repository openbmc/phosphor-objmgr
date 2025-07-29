#include "src/associations.hpp"

#include <iostream>

// Some debug functions for dumping out the main data structures in objmgr

void dumpAssociationOwnersType(AssociationOwnersType& assocOwners)
{
    using namespace std;
    cout << "##### AssociationOwnersType #####\n";
    for (const auto& i : assocOwners)
    {
        cout << "------------------------------------\n";
        cout << setw(15) << left << "OBJ PATH:" << i.first << '\n';

        for (const auto& j : i.second)
        {
            cout << setw(16) << left << "DBUS SERVICE:" << j.first << '\n';

            for (const auto& k : j.second)
            {
                cout << setw(17) << left << "ASSOC PATH:" << k.first << '\n';

                for (const auto& l : k.second)
                {
                    cout << setw(18) << left << "ENDPOINT:" << l << '\n';
                }
            }
        }
        cout << "------------------------------------\n";
    }
}

void dumpAssociationInterfaces(AssociationInterfaces& assocInterfaces)
{
    using namespace std;
    cout << "##### AssociationInterfaces #####\n";
    for (auto i : assocInterfaces)
    {
        cout << "------------------------------------\n";
        cout << setw(15) << left << "OBJ PATH:" << i.first << '\n';
        auto intfEndpoints = std::get<endpointsPos>(i.second);

        for (const auto& k : intfEndpoints)
        {
            cout << setw(16) << left << "ENDPOINTS:" << k << '\n';
        }
        cout << "------------------------------------\n";
    }
}

void dumpInterfaceMapType(InterfaceMapType& intfMap)
{
    using namespace std;
    cout << "##### interfaceMapType #####\n";
    for (const auto& i : intfMap)
    {
        cout << "------------------------------------\n";
        cout << setw(15) << left << "OBJ PATH:" << i.first << '\n';

        for (const auto& j : i.second)
        {
            cout << setw(16) << left << "DBUS SERVICE:" << j.first << '\n';

            for (const auto& k : j.second)
            {
                cout << setw(18) << left << "INTERFACE:" << k << '\n';
            }
        }
    }
    cout << "------------------------------------\n";
}
