all: cxtest ptest futest 

clean:
	-rm -f ptest cxtest futest *.o *.a

ptest: ptest.cc
	g++ -g -o ptest ptest.cc -pthread

cxtest: cxtest.cc
	g++ -g -o cxtest cxtest.cc

futest: futest.cc
	g++ -g -o futest futest.cc -pthread
