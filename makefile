all: libthread.a ttest mtest eptest timertest pipetest ptest locktest iftest threadpooltest

DESTDIR=../export

INCLS=thread.h threadmutex.h threadpipe.h osp.h dqueue.h epoll.h threadtimer.h spinlock.h ospnew.h ospnet.h threadpool.h

CFLAGS=-g -Wall

install: all
	-mkdir $(DESTDIR)/include $(DESTDIR)/lib $(DESTDIR)/bin
	cp -up $(INCLS) $(DESTDIR)/include
	cp -up libthread.a $(DESTDIR)/lib

clean:
	-rm -f iftest ptest ttest mtest eptest timertest pipetest locktest threadpooltest *.o *.a *temp.s
	(cd alternatives; make clean)

ospnet.o: ospnet.cc ospnet.h
	g++ -c $(CFLAGS) ospnet.cc

iftest.o: iftest.cc
	g++ -c $(CFLAGS) -o iftest.o iftest.cc

iftest: iftest.o ospnet.o
	g++ -o iftest iftest.o ospnet.o -pthread

getcontext.o: getcontext.s
	cpp getcontext.s >getcontext-temp.s
	as -o getcontext.o getcontext-temp.s
	-rm getcontext-temp.s

setcontext.o: setcontext.s
	cpp setcontext.s >setcontext-temp.s
	as -o setcontext.o setcontext-temp.s
	-rm setcontext-temp.s

osp.o: osp.cc $(INCLS)
	g++ -c $(CFLAGS) osp.cc -pthread

ospnew.o: ospnew.cc $(INCLS)
	g++ -c $(CFLAGS) ospnew.cc -pthread

threadtimer.o: threadtimer.cc $(INCLS)
	g++ -c $(CFLAGS) threadtimer.cc -pthread

threadmutex.o: threadmutex.cc $(INCLS)
	g++ -c $(CFLAGS) threadmutex.cc -pthread

threadpipe.o: threadpipe.cc $(INCLS)
	g++ -c $(CFLAGS) threadpipe.cc -pthread

libthread.a: epoll.o thread.o getcontext.o setcontext.o threadmutex.o threadpipe.o osp.o ospnew.o ospnet.o threadtimer.o threadpool.o
	$(AR) cr libthread.a epoll.o thread.o getcontext.o setcontext.o threadmutex.o threadpipe.o osp.o ospnew.o ospnet.o threadtimer.o threadpool.o
	$(RANLIB) libthread.a

thread.o: thread.cc $(INCLS)
	g++ -c $(CFLAGS) -o thread.o thread.cc -pthread

epoll.o: epoll.cc $(INCLS)
	g++ -c $(CFLAGS) -o epoll.o epoll.cc -pthread

threadpool.o: threadpool.cc $(INCLS)
	g++ -c $(CFLAGS) -o threadpool.o threadpool.cc

threadpooltest.o: threadpooltest.cc $(INCLS)
	g++ -c $(CFLAGS) -o threadpooltest.o threadpooltest.cc

threadpooltest: threadpooltest.o libthread.a
	g++ $(CFLAGS) -o threadpooltest threadpooltest.o libthread.a -pthread

ttest.o: ttest.cc $(INCLS)
	g++ -c $(CFLAGS) -o ttest.o ttest.cc -pthread

ttest: ttest.o libthread.a
	g++ -g -o ttest ttest.o libthread.a -pthread

timertest.o: timertest.cc $(INCLS)
	g++ -c $(CFLAGS) -o timertest.o timertest.cc -pthread

pipetest.o: pipetest.cc $(INCLS)
	g++ -c $(CFLAGS) -o pipetest.o pipetest.cc -pthread

mtest.o: mtest.cc $(INCLS)
	g++ -c $(CFLAGS) -o mtest.o mtest.cc -pthread

ptest.o: ptest.cc $(INCLS)
	g++ -c $(CFLAGS) -o ptest.o ptest.cc -pthread

eptest.o: eptest.cc $(INCLS)
	g++ -c $(CFLAGS) -o eptest.o eptest.cc -pthread

locktest.o: locktest.cc $(INCLS)
	g++ -c $(CFLAGS) -o locktest.o locktest.cc -pthread

mtest: mtest.o libthread.a
	g++ -g -o mtest mtest.o libthread.a -pthread

ptest: ptest.o libthread.a
	g++ -g -o ptest ptest.o libthread.a -pthread

eptest: eptest.o libthread.a
	g++ -g -o eptest eptest.o libthread.a -pthread

timertest: timertest.o libthread.a
	g++ -g -o timertest timertest.o libthread.a -pthread

pipetest: pipetest.o libthread.a
	g++ -g -o pipetest pipetest.o libthread.a -pthread

locktest: locktest.o libthread.a
	g++ -g -o locktest locktest.o libthread.a -pthread

