project(
    'phosphor-objmgr',
    'c', 'cpp',
    default_options: [
        'buildtype=debugoptimized',
        'cpp_std=c++23',
        'warning_level=3',
        'werror=true',
    ],
    license: 'Apache-2.0',
    meson_version: '>=1.1.1',
    version: '1.0',
)

cxx = meson.get_compiler('cpp')

if cxx.has_header('CLI/CLI.hpp')
    cli11_dep = declare_dependency()
else
    cli11_dep = dependency('cli11')
endif
phosphor_logging = dependency('phosphor-logging')
phosphor_dbus_interfaces = dependency('phosphor-dbus-interfaces')
sdbusplus = dependency('sdbusplus')
boost = dependency('boost', version : '>=1.79.0', required: false, include_type: 'system')
if not boost.found()
  subproject('boost', required: false)
  boost_inc = include_directories('subprojects/boost_1_79_0/', is_system:true)
  boost  = declare_dependency(include_directories : boost_inc)
  boost = boost.as_system('system')
endif
add_project_arguments(
    [
        '-DBOOST_ASIO_DISABLE_THREADS',
        '-DBOOST_ASIO_NO_DEPRECATED',
    ],
    language: 'cpp',
)

if get_option('buildtype').startswith('debug')
    add_project_arguments('-DMAPPER_ENABLE_DEBUG', language : 'cpp')
endif

if get_option('tests').allowed()
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
                not get_option('tests').allowed(),
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

mapper_dep = declare_dependency(
    link_with: libmapper,
    include_directories: include_directories('libmapper'),
    dependencies: [ dependency('libsystemd') ],
)

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

mapperx = executable(
    'mapperx',
    [
        'src/main.cpp',
        'src/processing.cpp',
        'src/associations.cpp',
        'src/handler.cpp',
    ],
    dependencies: [
        boost,
        dependency('libsystemd'),
        phosphor_dbus_interfaces,
        phosphor_logging,
        sdbusplus,
        dependency('threads'),
        dependency('tinyxml2', default_options: ['tests=false']),
    ],
    install: true,
    install_dir: join_paths(
        get_option('prefix'), get_option('libexecdir'), meson.project_name())
)
meson.override_find_program('mapperx', mapperx)

systemd_system_unit_dir = dependency('systemd').get_variable(
    'systemdsystemunitdir'
)

conf = configuration_data()
conf.set('BINDIR', join_paths(get_option('prefix'), get_option('bindir')))
conf.set('LIBEXECDIR', join_paths(
    get_option('prefix'), get_option('libexecdir')))

unit_files = [
    'xyz.openbmc_project.ObjectMapper.service',
    'mapper-subtree-remove@.service',
    'mapper-wait@.service'
]

foreach u : unit_files
    configure_file(
        configuration: conf,
        input : join_paths('src/systemd', u) + '.in',
        install : true,
        install_dir: systemd_system_unit_dir,
        output : u
    )
endforeach

dbus_system_bus_services_dir = dependency('dbus-1').get_variable(
    'system_bus_services_dir',
    pkgconfig_define: ['prefix', get_option('prefix')]
)

install_data(
    'src/dbus/xyz.openbmc_project.ObjectMapper.service',
    install_dir: dbus_system_bus_services_dir)

install_data(
    'src/dbus/xyz.openbmc_project.ObjectMapper.conf',
    install_dir: get_option('datadir') / 'dbus-1' / 'system.d')

if not get_option('unit-failure-monitor').disabled()
    executable(
        'phosphor-unit-failure-monitor',
        [
            'fail-monitor/main.cpp',
            'fail-monitor/monitor.cpp',
        ],
        dependencies: [
            cli11_dep,
            phosphor_logging,
        ],
        install: true
    )
endif
