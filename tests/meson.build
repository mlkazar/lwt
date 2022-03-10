test_osp = executable('test_osp',
    ['test_main.cc','test_ospnew.cc'],
    dependencies: [lwt_dep, lwt_gtest_dep],
    include_directories: include_directories('..'),
)

test_lwt = executable('test_lwt',
    ['test_main.cc','test_lwt_pthread.cc'],
    dependencies: [lwt_dep, lwt_gtest_dep],
    include_directories: include_directories('..')
)

test('test_osp', test_osp)
test('test_lwt', test_lwt)
