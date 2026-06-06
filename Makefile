CC=gcc
CFLAGS=-Wall -Wextra -std=c99

all: libDiskTest

libDiskTest: libDiskTest.o libDisk.o
	$(CC) $(CFLAGS) -o libDiskTest libDiskTest.o libDisk.o

libDiskTest.o: libDiskTest.c libDisk.h
	$(CC) $(CFLAGS) -c libDiskTest.c

libDisk.o: libDisk.c libDisk.h
	$(CC) $(CFLAGS) -c libDisk.c

clean:
	rm -f *.o libDiskTest test.disk