project(
    'phosphor-objmgr',
    'c', 'cpp',
    default_options: [
        'cpp_std=c++20',
        'warning_level=3',
        'werror=true',
        'tests=' + (meson.is_subproject() ? 'disabled' : 'auto'),
        'libonly=' + (meson.is_subproject() ? 'true' : 'false'),
    ],
    license: 'Apache-2.0',
    meson_version: '>=0.57.0',
    version: '1.0',
)

if not get_option('tests').disabled()
    gtest = dependency('gtest', main: true, disabler: true, required: false)
    if not gtest.found()
        gtest_proj = import('cmake').subproject('googletest', required: false)
        if gtest_proj.found()
            gtest = declare_dependency(
                dependencies: [
                    dependency('threads'),
                    gtest_proj.dependency('gtest'),
                    gtest_proj.dependency('gtest_main'),
                ]
            )
        else
            assert(
                not get_option('tests').enabled(),
                'Googletest is required if tests are enabled'
            )
        endif
    endif
endif

subdir('libmapper')
if not get_option('libonly')
    subdir('mapper')
    subdir('mapperx')
    subdir('fail-monitor')
endif
