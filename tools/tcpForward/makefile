all : single.out single.dbg
single.out : singleThread.cpp
	gcc -DRUN -O2 -o single.out singleThread.cpp -I../include -lpthread

single.dbg : singleThread.cpp
	gcc -DDBG -o $@ -I../include -lpthread $^ 
clean : 
	rm single.dbg single.out
