test_osp = executable('test_osp',
	   ['test_osp.cc','test_ospnew.cc'],
       include_directories: include_directories('..'),
	   #link_with: [lwt_lib],
	   link_args: ['-lpthread', '-lgtest']
	   )

test('test_osp', test_osp)
