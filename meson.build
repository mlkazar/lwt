subdir('alternatives')

lwt_headers = '''
  thread.h
  threadmutex.h
  threadpipe.h
  osp.h
  dqueue.h
  epoll.h
  threadtimer.h
  spinlock.h
  ospnew.h
  ospnet.h
'''.split()

lwt_sources = '''
  epoll.cc
  thread.cc
  threadmutex.cc
  threadpipe.cc
  osp.cc
  ospnew.cc
  ospnet.cc
  threadtimer.cc
'''.split()

getcontext = asm_gen.process('getcontext.s')
setcontext = asm_gen.process('setcontext.s')

lwt_lib = static_library('thread',
		     lwt_sources + [getcontext,setcontext],
		     install: false)

executable('iftest', 
  'iftest.cc', 
  link_with: lwt_lib)

executable('ttest', 
  'ttest.cc', 
  link_with: lwt_lib, 
  link_args: '-lpthread')

executable('timertest',
  'timertest.cc',
   link_with: lwt_lib,
   link_args: '-lpthread')

executable('pipetest',
  'pipetest.cc',
  link_with: lwt_lib, 
  link_args: '-lpthread')

executable('mtest',
  'mtest.cc',
  link_with: lwt_lib,
   link_args: '-lpthread')

executable('ptest',
  'ptest.cc',
  link_with: lwt_lib,
  link_args: '-lpthread')

executable('eptest',
   'eptest.cc',
   link_with: lwt_lib,
   link_args: '-lpthread')

executable('locktest',
  'locktest.cc',
  link_with: lwt_lib,
  link_args: '-lpthread')

install_headers(lwt_headers)