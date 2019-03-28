all: ttest libthread.a

clean:
	-rm -f ttest *.o *.a *temp.s
	(cd alternatives; make clean)

getcontext.o: getcontext.s
	cpp getcontext.s >getcontext-temp.s
	as -o getcontext.o getcontext-temp.s
	-rm getcontext-temp.s

setcontext.o: setcontext.s
	cpp setcontext.s >setcontext-temp.s
	as -o setcontext.o setcontext-temp.s
	-rm setcontext-temp.s

libthread.a: thread.o getcontext.o setcontext.o
	ar cr libthread.a thread.o getcontext.o setcontext.o
	ranlib libthread.a

ttest.o: thread.h ttest.cc
	g++ -c -g -o ttest.o ttest.cc -pthread

thread.o: thread.cc thread.h
	g++ -c -g -o thread.o thread.cc -pthread

ttest: ttest.o libthread.a
	g++ -g -o ttest ttest.o libthread.a -pthread
