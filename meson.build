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

lwt_srcs = '''
    epoll.cc
    thread.cc
    threadmutex.cc
    threadpipe.cc
    osp.cc
    ospnew.cc
    ospnet.cc
    threadtimer.cc
    threadpool.cc
    lwt_pthread.cc
'''.split()

getcontext = custom_target('getcontext',command: cppasm_command, input: ['getcontext.s'], output: ['getcontext-temp.s','getcontext.o'])
setcontext = custom_target('setcontext',command: cppasm_command, input: ['setcontext.s'], output: ['setcontext-temp.s','setcontext.o'])

lwt_lib = static_library('thread',
    [lwt_srcs, getcontext, setcontext],
    install: false
)

if get_variable('deps_hack',true)
    lwt_dep = declare_dependency(
        link_with: [lwt_lib],
        link_args: ['-pthread','-ldl']
    )
    lwt_gtest_dep = dependency('gtest')
else
    lwt_dep = declare_dependency(
        dependencies: [pthread_dep, dl_dep],
        link_with: [lwt_lib]
    )
    lwt_gtest_dep = gtest_dep
endif

executable('iftest', 
    'iftest.cc', 
    dependencies: [lwt_dep]
)

executable('ttest', 
    'ttest.cc',
    dependencies: lwt_dep
)

executable('timertest',
    'timertest.cc',
    dependencies: [lwt_dep]
)

executable('pipetest',
    'pipetest.cc',
    dependencies: [lwt_dep]
)

executable('mtest',
    'mtest.cc',
    dependencies: [lwt_dep]
)

executable('ptest',
    'ptest.cc',
    dependencies: [lwt_dep]
)

executable('eptest',
   'eptest.cc',
    dependencies: [lwt_dep]
)

executable('locktest',
    'locktest.cc',
    dependencies: [lwt_dep]
)

executable('threadpooltest',
    'threadpooltest.cc',
    dependencies: [lwt_dep]
)

install_headers(lwt_headers)

subdir('tests')
