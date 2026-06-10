#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "tinyFS.h"

/*
 * On-disk layout:
 *   superblock (block 0): byte 2 holds the head of the free block chain
 *   inode: name at bytes 4-12 (8 chars + NUL), size at 16, first data
 *          block at 20, read-only flag at 24
 *   data block: byte 2 links to the next data block of the file
 *   free block: byte 2 links to the next free block
 */

#define MAX_OPEN_FILES 32
#define MAX_FILENAME 8
#define NO_BLOCK 0

/* name field needs MAX_FILENAME + 1 bytes so an 8 char name keeps its NUL */
#define INODE_NAME_START 4
#define INODE_SIZE_START 16
#define INODE_DATA_START 20
#define INODE_RO_FLAG 24

#define DATA_BYTES_PER_BLOCK 252

typedef struct {
    int inUse;
    int inodeBlock;
    int filePointer;
} OpenFileEntry;

static int mountedDisk = -1;
static int isMounted = 0;
static OpenFileEntry openFiles[MAX_OPEN_FILES];

static void clearBlock(char block[BLOCKSIZE]) {
    memset(block, 0, BLOCKSIZE);
}

static void initBlock(char block[BLOCKSIZE], unsigned char type) {
    clearBlock(block);
    block[BYTE_TYPE] = type;
    block[BYTE_MAGIC] = MAGIC_NUMBER;
    block[BYTE_LINK] = 0;
    block[BYTE_RESERVED] = 0;
}

static int blockCountFromBytes(int nBytes) {
    return nBytes / BLOCKSIZE;
}

static int isValidName(char *name) {
    int len;

    if (name == NULL) {
        return 0;
    }

    len = strlen(name);

    if (len < 1 || len > MAX_FILENAME) {
        return 0;
    }

    for (int i = 0; i < len; i++) {
        if (!isalnum((unsigned char)name[i])) {
            return 0;
        }
    }

    return 1;
}

static int getFileSize(char inode[BLOCKSIZE]) {
    int size = 0;
    memcpy(&size, &inode[INODE_SIZE_START], sizeof(int));
    return size;
}

static void setFileSize(char inode[BLOCKSIZE], int size) {
    memcpy(&inode[INODE_SIZE_START], &size, sizeof(int));
}

static int getFirstDataBlock(char inode[BLOCKSIZE]) {
    int blockNum = 0;
    memcpy(&blockNum, &inode[INODE_DATA_START], sizeof(int));
    return blockNum;
}

static void setFirstDataBlock(char inode[BLOCKSIZE], int blockNum) {
    memcpy(&inode[INODE_DATA_START], &blockNum, sizeof(int));
}

static int isReadOnly(char inode[BLOCKSIZE]) {
    return inode[INODE_RO_FLAG] == 1;
}

static void setReadOnly(char inode[BLOCKSIZE], int value) {
    inode[INODE_RO_FLAG] = value;
}

/* pops the first block off the free chain and returns its block number */
static int getFreeBlock(void) {
    char super[BLOCKSIZE];
    char freeBlock[BLOCKSIZE];
    int freeBlockNum;
    int nextFreeBlock;

    if (readBlock(mountedDisk, 0, super) < 0) {
        return TFS_ERR_DISK;
    }

    freeBlockNum = (unsigned char)super[BYTE_LINK];

    if (freeBlockNum == NO_BLOCK) {
        return TFS_ERR_NO_SPACE;
    }

    if (readBlock(mountedDisk, freeBlockNum, freeBlock) < 0) {
        return TFS_ERR_DISK;
    }

    nextFreeBlock = (unsigned char)freeBlock[BYTE_LINK];
    super[BYTE_LINK] = nextFreeBlock;

    if (writeBlock(mountedDisk, 0, super) < 0) {
        return TFS_ERR_DISK;
    }

    return freeBlockNum;
}

/* pushes a block onto the front of the free chain */
static int returnBlockToFreeList(int blockNum) {
    char super[BLOCKSIZE];
    char block[BLOCKSIZE];

    if (readBlock(mountedDisk, 0, super) < 0) {
        return TFS_ERR_DISK;
    }

    initBlock(block, BLOCK_FREE);
    block[BYTE_LINK] = super[BYTE_LINK];

    if (writeBlock(mountedDisk, blockNum, block) < 0) {
        return TFS_ERR_DISK;
    }

    super[BYTE_LINK] = blockNum;

    if (writeBlock(mountedDisk, 0, super) < 0) {
        return TFS_ERR_DISK;
    }

    return TFS_SUCCESS;
}

/* frees an entire chain of data blocks starting at firstDataBlock */
static int freeDataBlocks(int firstDataBlock) {
    char block[BLOCKSIZE];
    int current;
    int next;

    current = firstDataBlock;

    while (current != NO_BLOCK) {
        if (readBlock(mountedDisk, current, block) < 0) {
            return TFS_ERR_DISK;
        }

        next = (unsigned char)block[BYTE_LINK];

        if (returnBlockToFreeList(current) < 0) {
            return TFS_ERR_DISK;
        }

        current = next;
    }

    return TFS_SUCCESS;
}

/* scans every block for an inode whose name matches, returns its block number */
static int findInodeByName(char *name) {
    char block[BLOCKSIZE];

    for (int i = 1; i < 256; i++) {
        if (readBlock(mountedDisk, i, block) < 0) {
            break;
        }

        if ((unsigned char)block[BYTE_TYPE] == BLOCK_INODE &&
            strcmp(&block[INODE_NAME_START], name) == 0) {
            return i;
        }
    }

    return TFS_ERR_GENERAL;
}

static int getOpenFileSlot(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!openFiles[i].inUse) {
            return i;
        }
    }

    return TFS_ERR_NO_SPACE;
}

int tfs_mkfs(char *filename, int nBytes) {
    int disk;
    int totalBlocks;
    char block[BLOCKSIZE];

    if (filename == NULL || nBytes < BLOCKSIZE * 2) {
        return TFS_ERR_GENERAL;
    }

    totalBlocks = blockCountFromBytes(nBytes);

    /* block links are stored in a single byte, so block numbers above 255
       cannot be represented */
    if (totalBlocks > 256) {
        return TFS_ERR_GENERAL;
    }

    disk = openDisk(filename, nBytes);
    if (disk < 0) {
        return TFS_ERR_DISK;
    }

    initBlock(block, BLOCK_SUPER);

    if (totalBlocks > 1) {
        block[BYTE_LINK] = 1;
    }

    if (writeBlock(disk, 0, block) < 0) {
        closeDisk(disk);
        return TFS_ERR_DISK;
    }

    for (int i = 1; i < totalBlocks; i++) {
        initBlock(block, BLOCK_FREE);

        if (i + 1 < totalBlocks) {
            block[BYTE_LINK] = i + 1;
        } else {
            block[BYTE_LINK] = 0;
        }

        if (writeBlock(disk, i, block) < 0) {
            closeDisk(disk);
            return TFS_ERR_DISK;
        }
    }

    closeDisk(disk);

    return TFS_SUCCESS;
}

int tfs_mount(char *diskname) {
    char block[BLOCKSIZE];
    int disk;

    if (isMounted) {
        return TFS_ERR_ALREADY_MOUNTED;
    }

    if (diskname == NULL) {
        return TFS_ERR_GENERAL;
    }

    disk = openDisk(diskname, 0);
    if (disk < 0) {
        return TFS_ERR_DISK;
    }

    if (readBlock(disk, 0, block) < 0) {
        closeDisk(disk);
        return TFS_ERR_DISK;
    }

    if ((unsigned char)block[BYTE_TYPE] != BLOCK_SUPER ||
        (unsigned char)block[BYTE_MAGIC] != MAGIC_NUMBER) {
        closeDisk(disk);
        return TFS_ERR_BAD_FS;
    }

    mountedDisk = disk;
    isMounted = 1;

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        openFiles[i].inUse = 0;
        openFiles[i].inodeBlock = -1;
        openFiles[i].filePointer = 0;
    }

    return TFS_SUCCESS;
}

int tfs_unmount(void) {
    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (closeDisk(mountedDisk) < 0) {
        return TFS_ERR_DISK;
    }

    mountedDisk = -1;
    isMounted = 0;

    return TFS_SUCCESS;
}

fileDescriptor tfs_openFile(char *name) {
    char inode[BLOCKSIZE];
    int inodeBlock;
    int fd;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (!isValidName(name)) {
        return TFS_ERR_BAD_NAME;
    }

    inodeBlock = findInodeByName(name);

    if (inodeBlock < 0) {
        inodeBlock = getFreeBlock();

        if (inodeBlock < 0) {
            return inodeBlock;
        }

        initBlock(inode, BLOCK_INODE);

        strcpy(&inode[INODE_NAME_START], name);
        setFileSize(inode, 0);
        setFirstDataBlock(inode, NO_BLOCK);
        setReadOnly(inode, 0);

        if (writeBlock(mountedDisk, inodeBlock, inode) < 0) {
            return TFS_ERR_DISK;
        }
    }

    fd = getOpenFileSlot();

    if (fd < 0) {
        return fd;
    }

    openFiles[fd].inUse = 1;
    openFiles[fd].inodeBlock = inodeBlock;
    openFiles[fd].filePointer = 0;

    return fd;
}

int tfs_closeFile(fileDescriptor FD) {
    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (FD < 0 || FD >= MAX_OPEN_FILES || !openFiles[FD].inUse) {
        return TFS_ERR_BAD_FD;
    }

    openFiles[FD].inUse = 0;
    openFiles[FD].inodeBlock = -1;
    openFiles[FD].filePointer = 0;

    return TFS_SUCCESS;
}

int tfs_writeFile(fileDescriptor FD, char *buffer, int size) {
    char inode[BLOCKSIZE];
    char dataBlock[BLOCKSIZE];

    int oldFirstDataBlock;
    int firstNewDataBlock;
    int previousDataBlock;
    int currentDataBlock;

    int bytesWritten;
    int bytesToCopy;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (FD < 0 || FD >= MAX_OPEN_FILES || !openFiles[FD].inUse) {
        return TFS_ERR_BAD_FD;
    }

    if (buffer == NULL || size < 0) {
        return TFS_ERR_GENERAL;
    }

    if (readBlock(mountedDisk, openFiles[FD].inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    if ((unsigned char)inode[BYTE_TYPE] != BLOCK_INODE) {
        return TFS_ERR_BAD_FD;
    }

    if (isReadOnly(inode)) {
        return TFS_ERR_READ_ONLY;
    }

    oldFirstDataBlock = getFirstDataBlock(inode);

    if (freeDataBlocks(oldFirstDataBlock) < 0) {
        return TFS_ERR_DISK;
    }

    firstNewDataBlock = NO_BLOCK;
    previousDataBlock = NO_BLOCK;
    bytesWritten = 0;

    while (bytesWritten < size) {
        currentDataBlock = getFreeBlock();

        if (currentDataBlock < 0) {
            /* out of space partway through: free what was allocated and
               leave the file empty so the inode stays consistent */
            freeDataBlocks(firstNewDataBlock);
            setFileSize(inode, 0);
            setFirstDataBlock(inode, NO_BLOCK);
            writeBlock(mountedDisk, openFiles[FD].inodeBlock, inode);
            return currentDataBlock;
        }

        initBlock(dataBlock, BLOCK_DATA);

        bytesToCopy = size - bytesWritten;

        if (bytesToCopy > DATA_BYTES_PER_BLOCK) {
            bytesToCopy = DATA_BYTES_PER_BLOCK;
        }

        memcpy(&dataBlock[BYTE_DATA], &buffer[bytesWritten], bytesToCopy);

        if (firstNewDataBlock == NO_BLOCK) {
            firstNewDataBlock = currentDataBlock;
        }

        if (previousDataBlock != NO_BLOCK) {
            char previousBlock[BLOCKSIZE];

            if (readBlock(mountedDisk, previousDataBlock, previousBlock) < 0) {
                return TFS_ERR_DISK;
            }

            previousBlock[BYTE_LINK] = currentDataBlock;

            if (writeBlock(mountedDisk, previousDataBlock, previousBlock) < 0) {
                return TFS_ERR_DISK;
            }
        }

        if (writeBlock(mountedDisk, currentDataBlock, dataBlock) < 0) {
            return TFS_ERR_DISK;
        }

        previousDataBlock = currentDataBlock;
        bytesWritten += bytesToCopy;
    }

    setFileSize(inode, size);
    setFirstDataBlock(inode, firstNewDataBlock);

    if (writeBlock(mountedDisk, openFiles[FD].inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    openFiles[FD].filePointer = 0;

    return TFS_SUCCESS;
}

int tfs_deleteFile(fileDescriptor FD) {
    char inode[BLOCKSIZE];
    int inodeBlock;
    int firstDataBlock;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (FD < 0 || FD >= MAX_OPEN_FILES || !openFiles[FD].inUse) {
        return TFS_ERR_BAD_FD;
    }

    inodeBlock = openFiles[FD].inodeBlock;

    if (readBlock(mountedDisk, inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    if ((unsigned char)inode[BYTE_TYPE] != BLOCK_INODE) {
        return TFS_ERR_BAD_FD;
    }

    if (isReadOnly(inode)) {
        return TFS_ERR_READ_ONLY;
    }

    firstDataBlock = getFirstDataBlock(inode);

    if (freeDataBlocks(firstDataBlock) < 0) {
        return TFS_ERR_DISK;
    }

    if (returnBlockToFreeList(inodeBlock) < 0) {
        return TFS_ERR_DISK;
    }

    /* the same file may be open under several descriptors, so close
       every entry that points at the freed inode */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (openFiles[i].inUse && openFiles[i].inodeBlock == inodeBlock) {
            openFiles[i].inUse = 0;
            openFiles[i].inodeBlock = -1;
            openFiles[i].filePointer = 0;
        }
    }

    return TFS_SUCCESS;
}

int tfs_readByte(fileDescriptor FD, char *buffer) {
    char inode[BLOCKSIZE];
    char dataBlock[BLOCKSIZE];

    int fileSize;
    int firstDataBlock;
    int currentDataBlock;

    int targetOffset;
    int blockOffset;
    int byteOffsetInsideBlock;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (FD < 0 || FD >= MAX_OPEN_FILES || !openFiles[FD].inUse) {
        return TFS_ERR_BAD_FD;
    }

    if (buffer == NULL) {
        return TFS_ERR_GENERAL;
    }

    if (readBlock(mountedDisk, openFiles[FD].inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    if ((unsigned char)inode[BYTE_TYPE] != BLOCK_INODE) {
        return TFS_ERR_BAD_FD;
    }

    fileSize = getFileSize(inode);

    if (openFiles[FD].filePointer >= fileSize) {
        return TFS_ERR_EOF;
    }

    firstDataBlock = getFirstDataBlock(inode);
    currentDataBlock = firstDataBlock;

    targetOffset = openFiles[FD].filePointer;
    blockOffset = targetOffset / DATA_BYTES_PER_BLOCK;
    byteOffsetInsideBlock = targetOffset % DATA_BYTES_PER_BLOCK;

    /* follow the chain of data blocks until the one holding the offset */
    for (int i = 0; i < blockOffset; i++) {
        if (readBlock(mountedDisk, currentDataBlock, dataBlock) < 0) {
            return TFS_ERR_DISK;
        }

        currentDataBlock = (unsigned char)dataBlock[BYTE_LINK];

        if (currentDataBlock == NO_BLOCK) {
            return TFS_ERR_DISK;
        }
    }

    if (readBlock(mountedDisk, currentDataBlock, dataBlock) < 0) {
        return TFS_ERR_DISK;
    }

    *buffer = dataBlock[BYTE_DATA + byteOffsetInsideBlock];

    openFiles[FD].filePointer++;

    return TFS_SUCCESS;
}

int tfs_seek(fileDescriptor FD, int offset) {
    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (FD < 0 || FD >= MAX_OPEN_FILES || !openFiles[FD].inUse) {
        return TFS_ERR_BAD_FD;
    }

    if (offset < 0) {
        return TFS_ERR_GENERAL;
    }

    openFiles[FD].filePointer = offset;

    return TFS_SUCCESS;
}

int tfs_readdir(void) {
    char block[BLOCKSIZE];
    int foundAny = 0;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    printf("Files on TinyFS disk:\n");

    for (int i = 1; i < 256; i++) {
        if (readBlock(mountedDisk, i, block) < 0) {
            break;
        }

        if ((unsigned char)block[BYTE_TYPE] == BLOCK_INODE) {
            printf("  %s\n", &block[INODE_NAME_START]);
            foundAny = 1;
        }
    }

    if (!foundAny) {
        printf("  <empty>\n");
    }

    return TFS_SUCCESS;
}

int tfs_rename(fileDescriptor FD, char *newName) {
    char inode[BLOCKSIZE];

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (FD < 0 || FD >= MAX_OPEN_FILES || !openFiles[FD].inUse) {
        return TFS_ERR_BAD_FD;
    }

    if (!isValidName(newName)) {
        return TFS_ERR_BAD_NAME;
    }

    if (findInodeByName(newName) >= 0) {
        return TFS_ERR_GENERAL;
    }

    if (readBlock(mountedDisk, openFiles[FD].inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    if ((unsigned char)inode[BYTE_TYPE] != BLOCK_INODE) {
        return TFS_ERR_BAD_FD;
    }

    memset(&inode[INODE_NAME_START], 0, MAX_FILENAME + 1);
    strcpy(&inode[INODE_NAME_START], newName);

    if (writeBlock(mountedDisk, openFiles[FD].inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    return TFS_SUCCESS;
}

int tfs_makeRO(char *name) {
    char inode[BLOCKSIZE];
    int inodeBlock;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (!isValidName(name)) {
        return TFS_ERR_BAD_NAME;
    }

    inodeBlock = findInodeByName(name);

    if (inodeBlock < 0) {
        return TFS_ERR_GENERAL;
    }

    if (readBlock(mountedDisk, inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    setReadOnly(inode, 1);

    if (writeBlock(mountedDisk, inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    return TFS_SUCCESS;
}

int tfs_makeRW(char *name) {
    char inode[BLOCKSIZE];
    int inodeBlock;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (!isValidName(name)) {
        return TFS_ERR_BAD_NAME;
    }

    inodeBlock = findInodeByName(name);

    if (inodeBlock < 0) {
        return TFS_ERR_GENERAL;
    }

    if (readBlock(mountedDisk, inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    setReadOnly(inode, 0);

    if (writeBlock(mountedDisk, inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    return TFS_SUCCESS;
}

/* writes one byte at the current file pointer and advances it; cannot
   grow the file past its existing size */
int tfs_writeByte(fileDescriptor FD, unsigned int data) {
    char inode[BLOCKSIZE];
    char dataBlock[BLOCKSIZE];

    int fileSize;
    int currentDataBlock;

    int targetOffset;
    int blockOffset;
    int byteOffsetInsideBlock;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (FD < 0 || FD >= MAX_OPEN_FILES || !openFiles[FD].inUse) {
        return TFS_ERR_BAD_FD;
    }

    if (readBlock(mountedDisk, openFiles[FD].inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    if ((unsigned char)inode[BYTE_TYPE] != BLOCK_INODE) {
        return TFS_ERR_BAD_FD;
    }

    if (isReadOnly(inode)) {
        return TFS_ERR_READ_ONLY;
    }

    fileSize = getFileSize(inode);

    if (openFiles[FD].filePointer >= fileSize) {
        return TFS_ERR_EOF;
    }

    currentDataBlock = getFirstDataBlock(inode);

    targetOffset = openFiles[FD].filePointer;
    blockOffset = targetOffset / DATA_BYTES_PER_BLOCK;
    byteOffsetInsideBlock = targetOffset % DATA_BYTES_PER_BLOCK;

    for (int i = 0; i < blockOffset; i++) {
        if (readBlock(mountedDisk, currentDataBlock, dataBlock) < 0) {
            return TFS_ERR_DISK;
        }

        currentDataBlock = (unsigned char)dataBlock[BYTE_LINK];

        if (currentDataBlock == NO_BLOCK) {
            return TFS_ERR_DISK;
        }
    }

    if (readBlock(mountedDisk, currentDataBlock, dataBlock) < 0) {
        return TFS_ERR_DISK;
    }

    dataBlock[BYTE_DATA + byteOffsetInsideBlock] = (char)(data & 0xFF);

    if (writeBlock(mountedDisk, currentDataBlock, dataBlock) < 0) {
        return TFS_ERR_DISK;
    }

    openFiles[FD].filePointer++;

    return TFS_SUCCESS;
}