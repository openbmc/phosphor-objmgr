tests = [
    'well_known',
    'need_to_introspect',
    'associations',
    'name_change',
    'interfaces_added',
]

foreach t : tests
    test(t,
        executable(
            t.underscorify(),
            t + '.cpp',
            implicit_include_directories: false,
            dependencies: [
                gtest,
                mapperx_dep,
            ]))
endforeach
