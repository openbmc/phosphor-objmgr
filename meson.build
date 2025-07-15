project(
    'phosphor-objmgr',
    'c',
    'cpp',
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

if (cxx.get_id() == 'gcc')
    if (cxx.version().version_compare('<13.0'))
        error('This project requires gcc-13 or higher')
    endif
endif

# Enable debugging for debug builds
if get_option('buildtype').startswith('debug')
    add_project_arguments('-DMAPPER_ENABLE_DEBUG', language: 'cpp')
endif

# Boost configuration
add_project_arguments(
    ['-DBOOST_ASIO_DISABLE_THREADS', '-DBOOST_ASIO_NO_DEPRECATED'],
    language: 'cpp',
)

cli11 = dependency('CLI11', required: false, include_type: 'system')
if not cli11.found()
    cli11_proj = subproject('cli11', required: true)
    cli11 = cli11_proj.get_variable('CLI11_dep')
    cli11 = cli11.as_system('system')
endif

phosphor_logging = dependency('phosphor-logging')
phosphor_dbus_interfaces = dependency('phosphor-dbus-interfaces')
sdbusplus = dependency('sdbusplus')
boost = dependency(
    'boost',
    version: '>=1.87.0',
    required: false,
    include_type: 'system',
)
if not boost.found()
    cmake = import('cmake')
    opt = cmake.subproject_options()
    boost_libs = ['asio', 'callable_traits']
    opt.add_cmake_defines({'BOOST_INCLUDE_LIBRARIES': ';'.join(boost_libs)})

    boost_deps = []
    boost_proj = cmake.subproject('boost', required: true, options: opt)
    foreach boost_lib : boost_libs
        boost_lib_instance = boost_proj.dependency('boost_' + boost_lib).as_system()
        boost_deps += [boost_lib_instance]
    endforeach
    boost = declare_dependency(dependencies: boost_deps)
endif

if get_option('tests').allowed()
    gtest = dependency(
        'gtest_main',
        main: true,
        version: '>=1.15.2',
        required: true,
    )
    gmock = dependency('gmock', required: true)
    subdir('src/test')
    subdir('libmapper/test')
endif

install_headers('libmapper/mapper.h')

libmapper = library(
    'mapper',
    'libmapper/mapper.c',
    dependencies: [dependency('libsystemd')],
    gnu_symbol_visibility: 'hidden',
    version: meson.project_version(),
    install: true,
)

mapper_dep = declare_dependency(
    link_with: libmapper,
    include_directories: include_directories('libmapper'),
    dependencies: [dependency('libsystemd')],
)

import('pkgconfig').generate(
    name: 'libmapper',
    description: 'OpenBMC service discovery utility library',
    version: meson.project_version(),
    libraries: libmapper,
)

executable(
    'mapper',
    'libmapper/app.c',
    link_with: libmapper,
    dependencies: [dependency('libsystemd')],
    install: true,
)

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
        sdbusplus,
        dependency('threads'),
        dependency('tinyxml2', default_options: ['tests=false']),
    ],
    implicit_include_directories: false,
    install: true,
    install_dir: join_paths(
        get_option('prefix'),
        get_option('libexecdir'),
        meson.project_name(),
    ),
)
meson.override_find_program('mapperx', mapperx)

systemd_system_unit_dir = dependency('systemd').get_variable(
    'systemd_system_unit_dir',
)

conf = configuration_data()
conf.set('BINDIR', join_paths(get_option('prefix'), get_option('bindir')))
conf.set(
    'LIBEXECDIR',
    join_paths(get_option('prefix'), get_option('libexecdir')),
)

unit_files = [
    'xyz.openbmc_project.ObjectMapper.service',
    'mapper-subtree-remove@.service',
    'mapper-wait@.service',
]

foreach u : unit_files
    configure_file(
        configuration: conf,
        input: join_paths('src/systemd', u) + '.in',
        install: true,
        install_dir: systemd_system_unit_dir,
        output: u,
    )
endforeach

dbus_system_bus_services_dir = dependency('dbus-1').get_variable(
    'system_bus_services_dir',
    pkgconfig_define: ['prefix', get_option('prefix')],
)

install_data(
    'src/dbus/xyz.openbmc_project.ObjectMapper.service',
    install_dir: dbus_system_bus_services_dir,
)

install_data(
    'src/dbus/xyz.openbmc_project.ObjectMapper.conf',
    install_dir: get_option('datadir') / 'dbus-1' / 'system.d',
)

if not get_option('unit-failure-monitor').disabled()
    executable(
        'phosphor-unit-failure-monitor',
        ['fail-monitor/main.cpp', 'fail-monitor/monitor.cpp'],
        dependencies: [cli11, phosphor_logging, sdbusplus],
        implicit_include_directories: false,
        install: true,
    )
endif
