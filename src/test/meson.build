processing_cpp_dep = declare_dependency(sources: '../processing.cpp')
associations_cpp_dep = declare_dependency(sources: '../associations.cpp')

tests = [
  [ 'well_known', [ associations_cpp_dep, processing_cpp_dep ]],
  [ 'need_to_introspect', [ associations_cpp_dep, processing_cpp_dep ]],
  [ 'associations', [ associations_cpp_dep ]],
  [ 'name_change', [ associations_cpp_dep, processing_cpp_dep ]],
  [ 'interfaces_added', [ associations_cpp_dep, processing_cpp_dep ]],
]

foreach t : tests
  name = t[0]
  extra_deps = t[1]
  test(name, executable(name.underscorify(), name + '.cpp',
                     implicit_include_directories: false,
                     dependencies: [gtest, gmock, sdbusplus, extra_deps],
                     include_directories: ['../..']),
       workdir: meson.current_source_dir())
endforeach
