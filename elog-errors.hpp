// This file was autogenerated.  Do not edit!
// See elog-gen.py for more details
#pragma once

#include <string>
#include <tuple>
#include <type_traits>
#include <sdbusplus/exception.hpp>
#include <phosphor-logging/log.hpp>
#include <phosphor-logging/elog.hpp>

namespace sdbusplus
{
namespace xyz
{
namespace openbmc_project
{
namespace Fail
{
namespace Monitor
{
namespace Error
{
    struct DBusFailure;
} // namespace Error
} // namespace Monitor
} // namespace Fail
} // namespace openbmc_project
} // namespace xyz
} // namespace sdbusplus


namespace phosphor
{

namespace logging
{

namespace xyz
{
namespace openbmc_project
{
namespace Fail
{
namespace Monitor
{
namespace _DBusFailure
{

struct FAIL
{
    static constexpr auto str = "FAIL=%s";
    static constexpr auto str_short = "FAIL";
    using type = std::tuple<std::decay_t<decltype(str)>,const char*>;
    explicit constexpr FAIL(const char* a) : _entry(entry(str, a)) {};
    type _entry;
};
struct PATH
{
    static constexpr auto str = "PATH=%s";
    static constexpr auto str_short = "PATH";
    using type = std::tuple<std::decay_t<decltype(str)>,const char*>;
    explicit constexpr PATH(const char* a) : _entry(entry(str, a)) {};
    type _entry;
};

}  // namespace _DBusFailure

struct DBusFailure : public sdbusplus::exception_t
{
    static constexpr auto errName = "xyz.openbmc_project.Fail.Monitor.DBusFailure";
    static constexpr auto errDesc = "A DBus call failed";
    static constexpr auto L = level::ERR;
    using FAIL = _DBusFailure::FAIL;
    using PATH = _DBusFailure::PATH;
    using metadata_types = std::tuple<FAIL, PATH>;

    const char* name() const noexcept
    {
        return errName;
    }

    const char* description() const noexcept
    {
        return errDesc;
    }

    const char* what() const noexcept
    {
        return errName;
    }
};

} // namespace Monitor
} // namespace Fail
} // namespace openbmc_project
} // namespace xyz


namespace details
{

template <>
struct map_exception_type<sdbusplus::xyz::openbmc_project::Fail::Monitor::Error::DBusFailure>
{
    using type = xyz::openbmc_project::Fail::Monitor::DBusFailure;
};

}


} // namespace logging

} // namespace phosphor
