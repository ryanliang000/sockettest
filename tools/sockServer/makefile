all: single.out


single.out : singleThread.cpp
	g++ -O3 -DTMP -DREL -o single.out singleThread.cpp -I../include -lpthread -lleveldb -std=c++11

clean:
	rm single.out
