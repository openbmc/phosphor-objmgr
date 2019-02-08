#include "src/associations.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <gtest/gtest.h>
/** @class AsioServerClassTest
 *
 *  @brief Provide wrapper for creating asio::object_server for test suite
 */
class AsioServerClassTest : public testing::Test
{
  protected:
    // Make this global to the whole test suite since we want to share
    // the asio::object_server accross the test cases
    // NOTE - latest googltest changed to SetUpTestSuite()
    static void SetUpTestCase()
    {
        boost::asio::io_context io;
        auto conn = std::make_shared<sdbusplus::asio::connection>(io);

        conn->request_name("xyz.openbmc_project.ObjMgr.Test");
        server = new sdbusplus::asio::object_server(conn);
    }

    // NOTE - latest googltest changed to TearDownTestSuite()
    static void TearDownTestCase()
    {
        delete server;
        server = nullptr;
    }

    static sdbusplus::asio::object_server* server;
};
