all: server.out dbg.out

server.out : singleThread.cpp
	g++ -O2 -DREL -o $@ singleThread.cpp -I../include -lpthread -lleveldb -std=c++11

dbg.out : singleThread.cpp
	g++ -g -DTMP -DDBG -o $@ singleThread.cpp -I../include -lpthread -lleveldb -std=c++11

clean:
	rm server.out dbg.out
