all: cxtest ptest ttest libthread.a

clean:
	-rm -f ptest cxtest ttest *.o *.a

libthread.a: thread.o
	ar cr libthread.a thread.o
	ranlib libthread.a

ptest: ptest.cc
	g++ -g -o ptest ptest.cc -pthread

cxtest: cxtest.cc
	g++ -g -o cxtest cxtest.cc

ttest.o: thread.h ttest.cc
	g++ -c -g -o ttest.o ttest.cc -pthread

thread.o: thread.cc thread.h
	g++ -c -g -o thread.o thread.cc -pthread

ttest: ttest.o libthread.a
	g++ -g -o ttest ttest.o libthread.a -pthread

