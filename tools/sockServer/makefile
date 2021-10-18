all: single.out


single.out : singleThread.cpp
	gcc -O3 -DREL -o single.out singleThread.cpp -I../include -lpthread

clean:
	rm single.out
