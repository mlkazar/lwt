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

<<<<<<< HEAD
lwt_sources = '''
=======
lwt_srcs = '''
>>>>>>> thhicks-meson
  epoll.cc
  thread.cc
  threadmutex.cc
  threadpipe.cc
  osp.cc
  ospnew.cc
  ospnet.cc
  threadtimer.cc
<<<<<<< HEAD
'''.split()

getcontext = asm_gen.process('getcontext.s')
setcontext = asm_gen.process('setcontext.s')

lwt_lib = static_library('thread',
		     lwt_sources + [getcontext,setcontext],
=======
  threadpool.cc
'''.split()

getcontext = custom_target('getcontext',command: cppasm_command, input: ['getcontext.s'], output: ['getcontext-temp.s','getcontext.o'])
setcontext = custom_target('setcontext',command: cppasm_command, input: ['setcontext.s'], output: ['setcontext-temp.s','setcontext.o'])

lwt_lib = static_library('thread',
		     [lwt_srcs, getcontext, setcontext],
>>>>>>> thhicks-meson
		     install: false)

executable('iftest', 
  'iftest.cc', 
  link_with: lwt_lib)

executable('ttest', 
  'ttest.cc', 
  link_with: lwt_lib, 
<<<<<<< HEAD
  link_args: '-lpthread')
=======
  link_args: '-pthread')
>>>>>>> thhicks-meson

executable('timertest',
  'timertest.cc',
   link_with: lwt_lib,
<<<<<<< HEAD
   link_args: '-lpthread')
=======
   link_args: '-pthread')
>>>>>>> thhicks-meson

executable('pipetest',
  'pipetest.cc',
  link_with: lwt_lib, 
<<<<<<< HEAD
  link_args: '-lpthread')
=======
  link_args: '-pthread')
>>>>>>> thhicks-meson

executable('mtest',
  'mtest.cc',
  link_with: lwt_lib,
<<<<<<< HEAD
   link_args: '-lpthread')
=======
   link_args: '-pthread')
>>>>>>> thhicks-meson

executable('ptest',
  'ptest.cc',
  link_with: lwt_lib,
<<<<<<< HEAD
  link_args: '-lpthread')
=======
  link_args: '-pthread')
>>>>>>> thhicks-meson

executable('eptest',
   'eptest.cc',
   link_with: lwt_lib,
<<<<<<< HEAD
   link_args: '-lpthread')
=======
   link_args: '-pthread')
>>>>>>> thhicks-meson

executable('locktest',
  'locktest.cc',
  link_with: lwt_lib,
<<<<<<< HEAD
  link_args: '-lpthread')

install_headers(lwt_headers)
=======
  link_args: '-pthread')

executable('threadpooltest',
  'threadpooltest.cc',
  link_with: lwt_lib,
  link_args: '-pthread')

install_headers(lwt_headers)
>>>>>>> thhicks-meson
