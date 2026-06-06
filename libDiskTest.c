#include <stdio.h>
#include <string.h>

#include "libDisk.h"

int main(void) {
    char writeBuf[BLOCKSIZE];
    char readBuf[BLOCKSIZE];

    memset(writeBuf, 0, BLOCKSIZE);
    memset(readBuf, 0, BLOCKSIZE);

    strcpy(writeBuf, "hello disk");

    int disk = openDisk("test.disk", 10240);

    if (disk < 0) {
        printf("open failed\n");
        return 1;
    }

    if (writeBlock(disk, 0, writeBuf) < 0) {
        printf("write failed\n");
        return 1;
    }

    if (readBlock(disk, 0, readBuf) < 0) {
        printf("read failed\n");
        return 1;
    }

    printf("Read back: %s\n", readBuf);

    closeDisk(disk);

    return 0;
}