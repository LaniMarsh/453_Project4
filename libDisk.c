#include "libDisk.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define MAX_DISKS 16

static int diskFDs[MAX_DISKS] = {-1};

static int getFreeSlot(void) {
    for (int i = 0; i < MAX_DISKS; i++) {
        if (diskFDs[i] == -1)
            return i;
    }

    return -1;
}

int openDisk(char *filename, int nBytes) {
    int slot = getFreeSlot();

    if (slot < 0)
        return -1;

    int fd;

    if (nBytes == 0) {
        fd = open(filename, O_RDWR);

        if (fd < 0)
            return -1;
    } else {
        if (nBytes < BLOCKSIZE)
            return -1;

        int size = (nBytes / BLOCKSIZE) * BLOCKSIZE;

        fd = open(filename,
                  O_RDWR | O_CREAT | O_TRUNC,
                  0666);

        if (fd < 0)
            return -1;

        if (ftruncate(fd, size) < 0) {
            close(fd);
            return -1;
        }
    }

    diskFDs[slot] = fd;
    return slot;
}

int closeDisk(int disk) {
    if (disk < 0 || disk >= MAX_DISKS)
        return -1;

    if (diskFDs[disk] == -1)
        return -1;

    close(diskFDs[disk]);
    diskFDs[disk] = -1;

    return 0;
}

int readBlock(int disk, int bNum, void *block) {
    if (disk < 0 || disk >= MAX_DISKS)
        return -1;

    if (diskFDs[disk] == -1)
        return -1;

    off_t offset = (off_t)bNum * BLOCKSIZE;

    if (lseek(diskFDs[disk], offset, SEEK_SET) < 0)
        return -1;

    ssize_t bytesRead =
        read(diskFDs[disk], block, BLOCKSIZE);

    if (bytesRead != BLOCKSIZE)
        return -1;

    return 0;
}

int writeBlock(int disk, int bNum, void *block) {
    if (disk < 0 || disk >= MAX_DISKS)
        return -1;

    if (diskFDs[disk] == -1)
        return -1;

    off_t offset = (off_t)bNum * BLOCKSIZE;

    if (lseek(diskFDs[disk], offset, SEEK_SET) < 0)
        return -1;

    ssize_t bytesWritten =
        write(diskFDs[disk], block, BLOCKSIZE);

    if (bytesWritten != BLOCKSIZE)
        return -1;

    return 0;
}