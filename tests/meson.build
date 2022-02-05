test_osp = executable('test_osp',
    ['test_main.cc','test_ospnew.cc'],
    dependencies: [lwt_dep],
    include_directories: include_directories('..'),
    link_args: ['-lgtest']
)

test_lwt = executable('test_lwt',
    ['test_main.cc','test_lwt_pthread.cc'],
    dependencies: [lwt_dep],
    include_directories: include_directories('..'),
    link_args: ['-lgtest']
)

test('test_osp', test_osp)
test('test_lwt', test_lwt)
