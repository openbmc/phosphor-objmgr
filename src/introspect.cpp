#include "introspect.hpp"

#include <tinyxml2.h>

#include <iostream>

std::optional<IntrospectResult> parseIntrospectXml(const std::string& path,
                                                   const std::string& xml)
{
    tinyxml2::XMLDocument doc;

    tinyxml2::XMLError e = doc.Parse(xml.c_str());
    if (e != tinyxml2::XMLError::XML_SUCCESS)
    {
        std::cerr << "XML parsing failed\n";
        return std::nullopt;
    }

    tinyxml2::XMLElement* pRoot = doc.FirstChildElement("node");
    if (pRoot == nullptr)
    {
        std::cerr << "XML document did not contain any data\n";
        return std::nullopt;
    }

    IntrospectResult result;

    for (tinyxml2::XMLElement* pElement = pRoot->FirstChildElement("interface");
         pElement != nullptr;
         pElement = pElement->NextSiblingElement("interface"))
    {
        const char* ifaceName = pElement->Attribute("name");
        if (ifaceName == nullptr)
        {
            continue;
        }
        result.interfaces.emplace_back(ifaceName);
    }

    const std::string parentPath = path == "/" ? "" : path;
    for (tinyxml2::XMLElement* pElement = pRoot->FirstChildElement("node");
         pElement != nullptr; pElement = pElement->NextSiblingElement("node"))
    {
        const char* childName = pElement->Attribute("name");
        if (childName == nullptr)
        {
            continue;
        }
        result.childPaths.emplace_back(parentPath + "/" + childName);
    }

    return result;
}
