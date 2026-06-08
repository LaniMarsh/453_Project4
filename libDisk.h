#ifndef LIBDISK_H
#define LIBDISK_H

#define BLOCKSIZE 256

#define DISK_SUCCESS 0
#define DISK_ERR_GENERAL -1
#define DISK_ERR_BAD_DISK -2
#define DISK_ERR_BAD_BLOCK -3
#define DISK_ERR_BAD_SIZE -4
#define DISK_ERR_NO_SPACE -5
#define DISK_ERR_IO -6

int openDisk(char *filename, int nBytes);
int closeDisk(int disk);
int readBlock(int disk, int bNum, void *block);
int writeBlock(int disk, int bNum, void *block);

#endif