#include "src/associations.hpp"

#include <iostream>

// Some debug functions for dumping out the main data structures in objmgr

void dumpAssociationOwnersType(AssociationOwnersType& assocOwners)
{
    using namespace std;
    cout << "##### AssociationOwnersType #####" << endl;
    for (auto i : assocOwners)
    {
        cout << "------------------------------------" << endl;
        cout << setw(15) << left << "OBJ PATH:" << i.first << endl;

        for (auto j : i.second)
        {
            cout << setw(16) << left << "DBUS SERVICE:" << j.first << endl;

            for (auto k : j.second)
            {
                cout << setw(17) << left << "ASSOC PATH:" << k.first << endl;

                for (auto l : k.second)
                {
                    cout << setw(18) << left << "ENDPOINT:" << l << endl;
                }
            }
        }
        cout << "------------------------------------" << endl;
    }
}

void dumpAssociationInterfaces(AssociationInterfaces& assocInterfaces)
{
    using namespace std;
    cout << "##### AssociationInterfaces #####" << endl;
    for (auto i : assocInterfaces)
    {
        cout << "------------------------------------" << endl;
        cout << setw(15) << left << "OBJ PATH:" << i.first << endl;
        auto intfEndpoints = std::get<endpointsPos>(i.second);

        for (auto k : intfEndpoints)
        {
            cout << setw(16) << left << "ENDPOINTS:" << k << endl;
        }
        cout << "------------------------------------" << endl;
    }
}

void dumpInterfaceMapType(InterfaceMapType& intfMap)
{
    using namespace std;
    cout << "##### interfaceMapType #####" << endl;
    for (auto i : intfMap)
    {
        cout << "------------------------------------" << endl;
        cout << setw(15) << left << "OBJ PATH:" << i.first << endl;

        for (auto j : i.second)
        {
            cout << setw(16) << left << "DBUS SERVICE:" << j.first << endl;

            for (auto k : j.second)
            {
                cout << setw(18) << left << "INTERFACE:" << k << endl;
            }
        }
    }
    cout << "------------------------------------" << endl;
}
