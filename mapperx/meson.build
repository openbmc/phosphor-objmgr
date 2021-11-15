mapperx_pre = declare_dependency(
    include_directories: include_directories('.'),
    dependencies: [
        dependency('boost'),
        dependency('sdbusplus'),
        dependency('systemd'),
        dependency('tinyxml2'),
    ])

mapperx_lib = static_library(
    'mapperx',
    'associations.cpp',
    'argument.cpp',
    'processing.cpp',
    implicit_include_directories: false,
    dependencies: mapperx_pre)

mapperx_dep = declare_dependency(
    link_with: mapperx_lib,
    dependencies: mapperx_pre)

executable(
    'mapperx',
    'main.cpp',
    implicit_include_directories: false,
    dependencies: mapperx_dep,
    install: true)

if not get_option('tests').disabled()
    subdir('test')
endif
