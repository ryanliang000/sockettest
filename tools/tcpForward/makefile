all : single.out single.dbg
single.out : singleThread.cpp
	g++ -DRUN -O2 -o single.out singleThread.cpp -I../include -lpthread -lleveldb

single.dbg : singleThread.cpp
	g++ -DDBG -o $@ -I../include -lpthread $^ -lleveldb
clean : 
	rm single.dbg single.out
