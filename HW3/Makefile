CC := gcc

all: libmem test

libmem:
	$(CC) -c -fpic mem.c
	$(CC) -shared -o libmem.so mem.o

test:
	$(CC) testmem.c -lmem -lm -L. -o testmem

clean:
	rm *.so *.o testmem
