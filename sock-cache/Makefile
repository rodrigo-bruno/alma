all: sock-cache

sock-cache: sock-cache.o
	gcc -o sock-cache sock-cache.o -pthread

sock-cache.o: sock-cache.c
	gcc -c sock-cache.c

clean:
	rm sock-cache.o sock-cache
