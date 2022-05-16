test('test_osp',executable('test_osp',
    ['test_main.cc','test_ospnew.cc'],
    dependencies: [lwt_dep, gtest_dep],
    include_directories: include_directories('..'),
))

test('test_lwt',executable('test_lwt',
    ['test_main.cc','test_lwt_pthread.cc'],
    dependencies: [lwt_dep, gtest_dep],
    include_directories: include_directories('..')
))

test('test_exception_lwt',executable('test_exception_lwt',
    ['test_exception.cc','test_lwtmain.cc'],
    dependencies: [lwt_dep, gtest_dep, boost_stacktrace_dep, boost_process_dep],
    include_directories: include_directories('..')
))

test('test_exception_pthread',executable('test_exception_pthread',
    ['test_exception.cc','test_pthreadmain.cc'],
    dependencies: [lwt_dep, gtest_dep, boost_stacktrace_dep, boost_process_dep],
    include_directories: include_directories('..')
))

test('test_assert',executable('test_assert',
    ['test_assert.cc','test_lwtmain.cc'],
    dependencies: [lwt_dep, gtest_dep, boost_stacktrace_dep, boost_process_dep],
    include_directories: include_directories('..')
))

executable('force_abnormal_exit',
    ['force_abnormal_exit.cc'],
    dependencies: [lwt_dep, boost_stacktrace_dep],
    include_directories: include_directories('..')
)