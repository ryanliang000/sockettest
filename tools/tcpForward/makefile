all : local.out single.out
local.out : localThread.cpp
	g++ -DRUN -DLOCAL -O2 -o $@ $^ -I../include -lpthread -lleveldb -std=c++11

single.out : singleThread.cpp
	g++ -DRUN -O2 -o $@ -I../include -lpthread $^ -lleveldb -std=c++11

clean : 
	rm single.out local.out
