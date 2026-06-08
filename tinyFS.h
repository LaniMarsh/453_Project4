#ifndef TINYFS_H
#define TINYFS_H

#include "libDisk.h"
#include "tinyFS_errno.h"

#define DEFAULT_DISK_SIZE 10240
#define DEFAULT_DISK_NAME "tinyFSDisk"

#define MAGIC_NUMBER 0x44

#define BLOCK_SUPER 1
#define BLOCK_INODE 2
#define BLOCK_DATA 3
#define BLOCK_FREE 4

#define BYTE_TYPE 0
#define BYTE_MAGIC 1
#define BYTE_LINK 2
#define BYTE_RESERVED 3
#define BYTE_DATA 4

typedef int fileDescriptor;

int tfs_mkfs(char *filename, int nBytes);
int tfs_mount(char *diskname);
int tfs_unmount(void);

fileDescriptor tfs_openFile(char *name);
int tfs_closeFile(fileDescriptor FD);
int tfs_writeFile(fileDescriptor FD, char *buffer, int size);
int tfs_deleteFile(fileDescriptor FD);
int tfs_readByte(fileDescriptor FD, char *buffer);
int tfs_seek(fileDescriptor FD, int offset);

#endif