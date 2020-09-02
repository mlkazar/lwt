all: libthread.a ttest mtest eptest

DESTDIR=~/export

INCLS=thread.h threadmutex.h osp.h dqueue.h epoll.h

install: all
	-mkdir $(DESTDIR)/include $(DESTDIR)/lib $(DESTDIR)/bin
	cp -up $(INCLS) $(DESTDIR)/include
	cp -up libthread.a $(DESTDIR)/lib

clean:
	-rm -f ttest mtest eptest *.o *.a *temp.s
	(cd alternatives; make clean)

getcontext.o: getcontext.s
	cpp getcontext.s >getcontext-temp.s
	as -o getcontext.o getcontext-temp.s
	-rm getcontext-temp.s

setcontext.o: setcontext.s
	cpp setcontext.s >setcontext-temp.s
	as -o setcontext.o setcontext-temp.s
	-rm setcontext-temp.s

osp.o: osp.cc $(INCLS)
	g++ -c -g osp.cc -pthread

threadmutex.o: threadmutex.cc $(INCLS)
	g++ -c -g threadmutex.cc -pthread

libthread.a: epoll.o thread.o getcontext.o setcontext.o threadmutex.o osp.o
	ar cr libthread.a epoll.o thread.o getcontext.o setcontext.o threadmutex.o osp.o
	ranlib libthread.a

thread.o: thread.cc $(INCLS)
	g++ -c -g -o thread.o thread.cc -pthread

epoll.o: epoll.cc $(INCLS)
	g++ -c -g -o epoll.o epoll.cc -pthread

ttest.o: ttest.cc $(INCLS)
	g++ -c -g -o ttest.o ttest.cc -pthread

ttest: ttest.o libthread.a
	g++ -g -o ttest ttest.o libthread.a -pthread

mtest.o: mtest.cc $(INCLS)
	g++ -c -g -o mtest.o mtest.cc -pthread

eptest.o: eptest.cc $(INCLS)
	g++ -c -g -o eptest.o eptest.cc -pthread

mtest: mtest.o libthread.a
	g++ -g -o mtest mtest.o libthread.a -pthread

eptest: eptest.o libthread.a
	g++ -g -o eptest eptest.o libthread.a -pthread
