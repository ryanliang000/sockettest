all : single.out
single.out : singleThread.cpp
	gcc -DRUN -O2 -o single.out singleThread.cpp -I../include -lpthread

clean : 
	rm single.out
