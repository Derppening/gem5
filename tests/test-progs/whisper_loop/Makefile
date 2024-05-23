all: main64-static main64-dynamic

CC ?= gcc

main64-dynamic:
	$(CC) main.c -o main64-dynamic

main64-static:
	$(CC) main.c -o main64-static -static

clean:
	rm -f main64-static main64-dynamic
