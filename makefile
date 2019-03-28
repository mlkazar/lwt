all: ttest libthread.a

clean:
	-rm -f ttest *.o *.a
	(cd alternatives; make clean)

libthread.a: thread.o
	ar cr libthread.a thread.o
	ranlib libthread.a

ttest.o: thread.h ttest.cc
	g++ -c -g -o ttest.o ttest.cc -pthread

thread.o: thread.cc thread.h
	g++ -c -g -o thread.o thread.cc -pthread

ttest: ttest.o libthread.a
	g++ -g -o ttest ttest.o libthread.a -pthread
