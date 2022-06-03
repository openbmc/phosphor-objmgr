project(
    'phosphor-objmgr',
    'c', 'cpp',
    default_options: [
        'buildtype=debugoptimized',
        'cpp_std=c++20',
        'warning_level=3',
        'werror=true',
    ],
    license: 'Apache-2.0',
    meson_version: '>=0.57.0',
    version: '1.0',
)

feature_map = {
    'data_com_google'          : '-DMAPPER_ENABLE_OEM_COM_GOOGLE',
    'data_com_ibm'             : '-DMAPPER_ENABLE_OEM_COM_IBM',
    'data_com_intel'           : '-DMAPPER_ENABLE_OEM_COM_INTEL',
    'data_org_open_power'      : '-DMAPPER_ENABLE_OEM_ORG_OPENPOWER',
}

phosphor_logging = dependency('phosphor-logging')
phosphor_dbus_interfaces = dependency('phosphor-dbus-interfaces')
sdbusplus = dependency('sdbusplus')


if get_option('buildtype').startswith('debug')
    add_project_arguments('-DMAPPER_ENABLE_DEBUG', language : 'cpp')
endif

if get_option('tests').enabled()
    gtest = dependency('gtest', main: true, disabler: true, required: false)
    gmock = dependency('gmock', disabler: true, required: false)
    if not gtest.found() or not gmock.found()
        gtest_proj = import('cmake').subproject('googletest', required: false)
        if gtest_proj.found()
            gtest = declare_dependency(
                dependencies: [
                    dependency('threads'),
                    gtest_proj.dependency('gtest'),
                    gtest_proj.dependency('gtest_main'),
                ]
            )
            gmock = gtest_proj.dependency('gmock')
        else
            assert(
                not get_option('tests').enabled(),
                'Googletest is required if tests are enabled'
            )
        endif
    endif
    subdir('src/test')
    subdir('libmapper/test')
endif

install_headers('libmapper/mapper.h')

libmapper = library(
    'mapper',
    'libmapper/mapper.c',
    dependencies: [ dependency('libsystemd') ],
    gnu_symbol_visibility: 'hidden',
    version: meson.project_version(),
    install: true)

import('pkgconfig').generate(
    name: 'libmapper',
    description: 'OpenBMC service discovery utility library',
    version: meson.project_version(),
    libraries: libmapper)

executable(
    'mapper',
    'libmapper/app.c',
    link_with: libmapper,
    dependencies: [ dependency('libsystemd') ],
    install: true)

executable(
    'mapperx',
    [
        'src/main.cpp',
        'src/processing.cpp',
        'src/associations.cpp',
    ],
    dependencies: [
        dependency('boost'),
        dependency('libsystemd'),
        phosphor_dbus_interfaces,
        phosphor_logging,
        sdbusplus,
        dependency('systemd'),
        dependency('threads'),
        dependency('tinyxml2'),
    ],
    install: true
)

systemd_system_unit_dir = dependency('systemd').get_variable(
    pkgconfig : 'systemdsystemunitdir'
)

install_data(
    'src/systemd/xyz.openbmc_project.ObjectMapper.service',
    install_dir: systemd_system_unit_dir)

dbus_system_bus_services_dir = dependency('dbus-1').get_variable(
    pkgconfig: 'system_bus_services_dir'
)

install_data(
    'src/dbus/xyz.openbmc_project.ObjectMapper.service',
    install_dir: dbus_system_bus_services_dir)

install_data(
    'src/dbus/xyz.openbmc_project.ObjectMapper.conf',
    install_dir: get_option('datadir') / 'dbus-1' / 'system.d')

executable(
    'phosphor-unit-failure-monitor',
    [
        'fail-monitor/argument.cpp',
        'fail-monitor/main.cpp',
        'fail-monitor/monitor.cpp',
    ],
    dependencies: [
        phosphor_logging,
    ],
    install: true
)
