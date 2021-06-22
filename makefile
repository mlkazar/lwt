all: libthread.a ttest mtest eptest timertest pipetest ptest locktest iftest

ifndef RANLIB
RANLIB=ranlib
endif

DESTDIR=../export

INCLS=thread.h threadmutex.h threadpipe.h osp.h dqueue.h epoll.h threadtimer.h spinlock.h ospnew.h ospnet.h

CXXFLAGS=-g -Wall

install: all
	-mkdir $(DESTDIR)/include $(DESTDIR)/lib $(DESTDIR)/bin
	cp -up $(INCLS) $(DESTDIR)/include
	cp -up libthread.a $(DESTDIR)/lib

clean:
	-rm -f iftest ptest ttest mtest eptest timertest pipetest locktest *.o *.a *temp.s
	(cd alternatives; make clean)

ospnet.o: ospnet.cc ospnet.h
	$(CXX) -c $(CXXFLAGS) ospnet.cc

iftest.o: iftest.cc
	$(CXX) -c $(CXXFLAGS) -o iftest.o iftest.cc

iftest: iftest.o ospnet.o
	$(CXX) -o iftest iftest.o ospnet.o -pthread

getcontext.o: getcontext.s
	cpp getcontext.s >getcontext-temp.s
	as -o getcontext.o getcontext-temp.s
	-rm getcontext-temp.s

setcontext.o: setcontext.s
	cpp setcontext.s >setcontext-temp.s
	as -o setcontext.o setcontext-temp.s
	-rm setcontext-temp.s

osp.o: osp.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) osp.cc -pthread

ospnew.o: ospnew.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) ospnew.cc -pthread

threadtimer.o: threadtimer.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) threadtimer.cc -pthread

threadmutex.o: threadmutex.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) threadmutex.cc -pthread

threadpipe.o: threadpipe.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) threadpipe.cc -pthread

libthread.a: epoll.o thread.o getcontext.o setcontext.o threadmutex.o threadpipe.o osp.o ospnew.o ospnet.o threadtimer.o
	$(AR) cr libthread.a epoll.o thread.o getcontext.o setcontext.o threadmutex.o threadpipe.o osp.o ospnew.o ospnet.o threadtimer.o
	$(RANLIB) libthread.a

thread.o: thread.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) -o thread.o thread.cc -pthread

epoll.o: epoll.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) -o epoll.o epoll.cc -pthread

ttest.o: ttest.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) -o ttest.o ttest.cc -pthread

ttest: ttest.o libthread.a
	$(CXX) -g -o ttest ttest.o libthread.a -pthread

timertest.o: timertest.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) -o timertest.o timertest.cc -pthread

pipetest.o: pipetest.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) -o pipetest.o pipetest.cc -pthread

mtest.o: mtest.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) -o mtest.o mtest.cc -pthread

ptest.o: ptest.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) -o ptest.o ptest.cc -pthread

eptest.o: eptest.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) -o eptest.o eptest.cc -pthread

locktest.o: locktest.cc $(INCLS)
	$(CXX) -c $(CXXFLAGS) -o locktest.o locktest.cc -pthread

mtest: mtest.o libthread.a
	$(CXX) -g -o mtest mtest.o libthread.a -pthread

ptest: ptest.o libthread.a
	$(CXX) -g -o ptest ptest.o libthread.a -pthread

eptest: eptest.o libthread.a
	$(CXX) -g -o eptest eptest.o libthread.a -pthread

timertest: timertest.o libthread.a
	$(CXX) -g -o timertest timertest.o libthread.a -pthread

pipetest: pipetest.o libthread.a
	$(CXX) -g -o pipetest pipetest.o libthread.a -pthread

locktest: locktest.o libthread.a
	$(CXX) -g -o locktest locktest.o libthread.a -pthread
