CC=gcc
CFLAGS=-Wall -Wextra -std=c99

all: libDiskTest tinyFSDemo tinyFSTest

libDiskTest: libDiskTest.o libDisk.o
	$(CC) $(CFLAGS) -o libDiskTest libDiskTest.o libDisk.o

tinyFSDemo: tinyFSDemo.o libTinyFS.o libDisk.o
	$(CC) $(CFLAGS) -o tinyFSDemo tinyFSDemo.o libTinyFS.o libDisk.o

tinyFSTest: tinyFSTest.o libTinyFS.o libDisk.o
	$(CC) $(CFLAGS) -o tinyFSTest tinyFSTest.o libTinyFS.o libDisk.o

libDiskTest.o: libDiskTest.c libDisk.h
	$(CC) $(CFLAGS) -c libDiskTest.c

tinyFSDemo.o: tinyFSDemo.c tinyFS.h tinyFS_errno.h libDisk.h
	$(CC) $(CFLAGS) -c tinyFSDemo.c

tinyFSTest.o: tinyFSTest.c tinyFS.h tinyFS_errno.h libDisk.h
	$(CC) $(CFLAGS) -c tinyFSTest.c

libTinyFS.o: libTinyFS.c tinyFS.h tinyFS_errno.h libDisk.h
	$(CC) $(CFLAGS) -c libTinyFS.c

libDisk.o: libDisk.c libDisk.h
	$(CC) $(CFLAGS) -c libDisk.c

clean:
	rm -f *.o libDiskTest tinyFSDemo tinyFSTest test.disk tinyFSDisk testDisk junkDisk smallDsk