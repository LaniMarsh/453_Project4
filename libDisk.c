#define _XOPEN_SOURCE 700
#include "libDisk.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DISKS 16

static int diskFDs[MAX_DISKS];
static int diskSizes[MAX_DISKS];
static int initialized = 0;

static void initDiskTable(void) {
    if (initialized) {
        return;
    }

    for (int i = 0; i < MAX_DISKS; i++) {
        diskFDs[i] = -1;
        diskSizes[i] = 0;
    }

    initialized = 1;
}

static int getFreeSlot(void) {
    initDiskTable();

    for (int i = 0; i < MAX_DISKS; i++) {
        if (diskFDs[i] == -1) {
            return i;
        }
    }

    return DISK_ERR_NO_SPACE;
}

static int isValidDisk(int disk) {
    initDiskTable();

    if (disk < 0 || disk >= MAX_DISKS) {
        return 0;
    }

    return diskFDs[disk] != -1;
}

static int isValidBlock(int disk, int bNum) {
    int offset;

    if (bNum < 0) {
        return 0;
    }

    offset = bNum * BLOCKSIZE;

    return offset + BLOCKSIZE <= diskSizes[disk];
}

int openDisk(char *filename, int nBytes) {
    int fd;
    int slot;
    int usableSize;

    if (filename == 0) {
        return DISK_ERR_GENERAL;
    }

    slot = getFreeSlot();
    if (slot < 0) {
        return slot;
    }

    if (nBytes == 0) {
        struct stat fileInfo;

        fd = open(filename, O_RDWR);
        if (fd < 0) {
            return DISK_ERR_IO;
        }

        if (fstat(fd, &fileInfo) < 0) {
            close(fd);
            return DISK_ERR_IO;
        }

        usableSize = ((int)fileInfo.st_size / BLOCKSIZE) * BLOCKSIZE;
        if (usableSize < BLOCKSIZE) {
            close(fd);
            return DISK_ERR_BAD_SIZE;
        }
    } else {
        if (nBytes < BLOCKSIZE) {
            return DISK_ERR_BAD_SIZE;
        }

        usableSize = (nBytes / BLOCKSIZE) * BLOCKSIZE;

        fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            return DISK_ERR_IO;
        }

        if (ftruncate(fd, usableSize) < 0) {
            close(fd);
            return DISK_ERR_IO;
        }
    }

    diskFDs[slot] = fd;
    diskSizes[slot] = usableSize;

    return slot;
}

int closeDisk(int disk) {
    if (!isValidDisk(disk)) {
        return DISK_ERR_BAD_DISK;
    }

    if (close(diskFDs[disk]) < 0) {
        return DISK_ERR_IO;
    }

    diskFDs[disk] = -1;
    diskSizes[disk] = 0;

    return DISK_SUCCESS;
}

int readBlock(int disk, int bNum, void *block) {
    off_t offset;
    ssize_t bytesRead;

    if (!isValidDisk(disk)) {
        return DISK_ERR_BAD_DISK;
    }

    if (block == 0 || !isValidBlock(disk, bNum)) {
        return DISK_ERR_BAD_BLOCK;
    }

    offset = (off_t)bNum * BLOCKSIZE;

    if (lseek(diskFDs[disk], offset, SEEK_SET) < 0) {
        return DISK_ERR_IO;
    }

    bytesRead = read(diskFDs[disk], block, BLOCKSIZE);
    if (bytesRead != BLOCKSIZE) {
        return DISK_ERR_IO;
    }

    return DISK_SUCCESS;
}

int writeBlock(int disk, int bNum, void *block) {
    off_t offset;
    ssize_t bytesWritten;

    if (!isValidDisk(disk)) {
        return DISK_ERR_BAD_DISK;
    }

    if (block == 0 || !isValidBlock(disk, bNum)) {
        return DISK_ERR_BAD_BLOCK;
    }

    offset = (off_t)bNum * BLOCKSIZE;

    if (lseek(diskFDs[disk], offset, SEEK_SET) < 0) {
        return DISK_ERR_IO;
    }

    bytesWritten = write(diskFDs[disk], block, BLOCKSIZE);
    if (bytesWritten != BLOCKSIZE) {
        return DISK_ERR_IO;
    }

    return DISK_SUCCESS;
}