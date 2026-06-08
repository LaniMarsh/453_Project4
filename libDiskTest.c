#include <stdio.h>
#include <string.h>

#include "libDisk.h"

int main(void) {
    char writeBuf[BLOCKSIZE];
    char readBuf[BLOCKSIZE];
    int disk;
    int result;

    memset(writeBuf, 0, BLOCKSIZE);
    memset(readBuf, 0, BLOCKSIZE);
    strcpy(writeBuf, "hello disk");

    disk = openDisk("test.disk", 10240);
    if (disk < 0) {
        printf("open failed: %d\n", disk);
        return 1;
    }

    result = writeBlock(disk, 0, writeBuf);
    if (result < 0) {
        printf("write failed: %d\n", result);
        return 1;
    }

    result = readBlock(disk, 0, readBuf);
    if (result < 0) {
        printf("read failed: %d\n", result);
        return 1;
    }

    printf("Read back: %s\n", readBuf);

    result = writeBlock(disk, 40, writeBuf);
    if (result < 0) {
        printf("Correctly rejected block 40: %d\n", result);
    }

    closeDisk(disk);

    disk = openDisk("test.disk", 0);
    readBlock(disk, 0, readBuf);

    printf("Read after reopen: %s\n", readBuf);

    closeDisk(disk);

    return 0;
}