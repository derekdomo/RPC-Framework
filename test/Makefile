PROGS=server
CFLAGS+=-Wall

all: server mylib.so

server: 
	gcc -Wall -L../lib -I../include -o server server.c ../lib/libdirtree.so

mylib.o: mylib.c
	gcc -Wall -fPIC -DPIC -c -L../lib -I../include mylib.c

mylib.so: mylib.o
	ld -shared -o mylib.so mylib.o -ldl


clean:
	rm -f *.o *.so server
