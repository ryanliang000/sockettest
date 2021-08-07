all: single.out


single.out : singleThread.cpp
	gcc -DREL -o single.out singleThread.cpp -I../include -lpthread

clean:
	rm single.out
