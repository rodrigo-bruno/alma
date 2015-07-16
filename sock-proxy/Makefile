all: sock-proxy

sock-proxy: sock-proxy.o
	gcc -o sock-proxy sock-proxy.o -pthread

sock-proxy.o: sock-proxy.c
	gcc -c sock-proxy.c

clean:
	rm sock-proxy.o sock-proxy
