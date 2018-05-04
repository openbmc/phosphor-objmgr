cmake_minimum_required(VERSION 2.8.10 FATAL_ERROR)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

option(YOCTO_DEPENDENCIES "Use YOCTO depedencies system" OFF)

project(mapperx CXX)

# Enable link time optimization This is a temporary workaround because
# INTERPROCEDURAL_OPTIMIZATION isn't available until cmake 3.9. gcc-ar and gcc-
# ranlib are wrappers around ar and ranlib which add the lto plugin to the
# command line.
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  if(NOT CMAKE_BUILD_TYPE MATCHES Debug)
    STRING(REGEX REPLACE "ar$" "gcc-ar" CMAKE_AR ${CMAKE_AR})
    STRING(REGEX REPLACE "ranlib$" "gcc-ranlib" CMAKE_RANLIB ${CMAKE_RANLIB})
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto -fno-fat-lto-objects")

    # Reduce the binary size by removing unnecessary dynamic symbol table
    # entries
    SET(
      CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} \
        -fvisibility=hidden \
        -fvisibility-inlines-hidden \
        -Wl,--exclude-libs,ALL"
      )
  endif(NOT CMAKE_BUILD_TYPE MATCHES Debug)
endif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")

if(NOT ${YOCTO_DEPENDENCIES})
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/prefix)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/prefix/include)
  include_directories(${CMAKE_BINARY_DIR}/openbmc-sdbusplus
                      ${CMAKE_BINARY_DIR}/prefix/include)
  configure_file(CMakeLists.txt.in 3rdparty/CMakeLists.txt)
  execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
                  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/3rdparty)
  execute_process(COMMAND ${CMAKE_COMMAND} --build .
                  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/3rdparty)

  set(CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/prefix ${CMAKE_PREFIX_PATH})

  include_directories(${CMAKE_BINARY_DIR}/sdbusplus-src
                      ${CMAKE_BINARY_DIR}/prefix/include)

  set(WANT_TRANSACTION 0)

  configure_file(${CMAKE_BINARY_DIR}/sdbusplus-src/sdbusplus/server.hpp.in
                 ${CMAKE_BINARY_DIR}/prefix/include/sdbusplus/server.hpp @ONLY)
  configure_file(${CMAKE_BINARY_DIR}/sdbusplus-src/sdbusplus/bus.hpp.in
                 ${CMAKE_BINARY_DIR}/prefix/include/sdbusplus/bus.hpp @ONLY)

endif()

add_definitions(-DBOOST_ERROR_CODE_HEADER_ONLY)
add_definitions(-DBOOST_SYSTEM_NO_DEPRECATED)
add_definitions(-DBOOST_ALL_NO_LIB)
add_definitions(-DBOOST_NO_RTTI)
add_definitions(-DBOOST_NO_TYPEID)

find_package(Boost 1.66 REQUIRED)
include_directories(${BOOST_SRC_DIR})

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

find_package(tinyxml2 REQUIRED)

set(TEST_FILES tests/mapper_test.cpp)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(mapperx src/main.cpp ${SRC_FILES})
target_link_libraries(mapperx systemd pthread)
target_link_libraries(mapperx tinyxml2)
install(TARGETS mapperx DESTINATION bin)
