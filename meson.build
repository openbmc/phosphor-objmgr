project(
    'phosphor-objmgr',
    'c', 'cpp',
    version: '1.0',
    license: 'Apache-2.0',
    default_options: [
        'cpp_std=c++17',
        'werror=true',
    ],
)

phosphor_logging = dependency(
    'phosphor-logging',
    fallback: [
        'phosphor-logging',
        'phosphor_logging_dep'])
sdbusplus = dependency(
    'sdbusplus',
    fallback: ['sdbusplus', 'sdbusplus_dep'])

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
endif

conf = configuration_data()
conf.set_quoted('MAPPER_BUSNAME', 'xyz.openbmc_project.ObjectMapper')
conf.set_quoted('MAPPER_INTERFACE', 'xyz.openbmc_project.ObjectMapper')
conf.set_quoted('MAPPER_PATH', '/xyz/openbmc_project/object_mapper')
configure_file(output: 'config.h', configuration: conf)

install_headers('libmapper/mapper.h')

libmapper = library(
    'mapper',
    'libmapper/mapper.c',
    dependencies: [ dependency('libsystemd') ],
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
        'src/argument.cpp',
        'src/processing.cpp',
        'src/associations.cpp',
    ],
    dependencies: [
        dependency('boost'),
        dependency('libsystemd'),
        phosphor_logging,
        sdbusplus,
        dependency('systemd'),
        dependency('threads'),
        dependency('tinyxml2'),
    ],
    install: true
)

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
