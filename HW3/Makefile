CFLAGS := -fPIC 
CC := gcc

test: testmem
	LD_LIBRARY_PATH=. ./testmem

testmem: libmem.so
	$(CC) testmem.c -lmem -lm -L. -o testmem 

libmem.so: mem.o
	$(CC) -shared -o libmem.so mem.o

clean:
	$(RM) *.o *.so*
