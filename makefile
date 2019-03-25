all: lwptest ptest ttest

clean:
	-rm -f ptest lwptest ttest *.o

ptest: ptest.cc
	g++ -g -o ptest ptest.cc -pthread

lwptest: lwptest.cc
	g++ -g -o lwptest lwptest.cc

ttest.o: task.h ttest.cc
	g++ -c -g -o ttest.o ttest.cc -pthread

task.o: task.cc task.h
	g++ -c -g -o task.o task.cc -pthread

ttest: ttest.o task.o
	g++ -g -o ttest ttest.o task.o -pthread

