tests = [
    [ 'mapper', [ declare_dependency(sources: [ '../mapper.c', 'utils.c' ]) ]],
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
