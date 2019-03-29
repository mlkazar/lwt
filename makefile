all: libthread.a ttest mtest

INCLS=thread.h threadmutex.h

clean:
	-rm -f ttest mtest *.o *.a *temp.s
	(cd alternatives; make clean)

getcontext.o: getcontext.s
	cpp getcontext.s >getcontext-temp.s
	as -o getcontext.o getcontext-temp.s
	-rm getcontext-temp.s

setcontext.o: setcontext.s
	cpp setcontext.s >setcontext-temp.s
	as -o setcontext.o setcontext-temp.s
	-rm setcontext-temp.s

threadmutex.o: threadmutex.cc $(INCLS)
	g++ -c -g threadmutex.cc -pthread

libthread.a: thread.o getcontext.o setcontext.o threadmutex.o
	ar cr libthread.a thread.o getcontext.o setcontext.o threadmutex.o
	ranlib libthread.a

thread.o: thread.cc $(INCLS)
	g++ -c -g -o thread.o thread.cc -pthread

ttest.o: ttest.cc $(INCLS)
	g++ -c -g -o ttest.o ttest.cc -pthread

ttest: ttest.o libthread.a
	g++ -g -o ttest ttest.o libthread.a -pthread

mtest.o: mtest.cc $(INCLS)
	g++ -c -g -o mtest.o mtest.cc -pthread

mtest: mtest.o libthread.a
	g++ -g -o mtest mtest.o libthread.a -pthread

