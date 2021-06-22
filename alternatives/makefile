all: cxtest ptest futest 

clean:
	-rm -f ptest cxtest futest *.o *.a

ptest: ptest.cc
	$(CXX)  -g -o ptest ptest.cc -pthread

cxtest: cxtest.cc
	$(CXX) -g -o cxtest cxtest.cc

futest: futest.cc
	$(CXX) -g -o futest futest.cc -pthread
