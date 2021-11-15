install_headers('mapper.h')

mapper_deps = [
  dependency('libsystemd'),
]

mapper_pre = declare_dependency(
    include_directories: include_directories('.'),
    dependencies: mapper_deps)

mapper_internal_lib = static_library(
    'mapper_internal',
    'mapper.c',
    gnu_symbol_visibility: 'hidden',
    dependencies: mapper_pre,
    implicit_include_directories: false)

mapper_internal_dep = declare_dependency(
    link_with: mapper_internal_lib,
    dependencies: mapper_pre)

mapper_lib = library(
    'mapper',
    dependencies: mapper_pre,
    link_whole: mapper_internal_lib,
    implicit_include_directories: false,
    version: meson.project_version(),
    install: true)

mapper_dep = declare_dependency(
    link_with: mapper_lib,
    dependencies: mapper_pre)

mapper_reqs = []
foreach dep : mapper_deps
  if dep.type_name() == 'pkgconfig'
    mapper_reqs += dep
  endif
endforeach

import('pkgconfig').generate(
    mapper_lib,
    name: 'libmapper',
    description: 'OpenBMC service discovery utility library',
    version: meson.project_version(),
    requires: mapper_reqs)

if not get_option('tests').disabled()
    subdir('test')
endif
