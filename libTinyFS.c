#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "tinyFS.h"

/*
 * On-disk layout:
 *   superblock (block 0): byte 2 holds the head of the free block chain,
 *          byte 4 holds the block number of the root directory inode
 *   inode: name at bytes 4-12 (8 chars + NUL), size at 16, first data
 *          block at 20, read-only flag at 24, directory flag at 25,
 *          parent directory block at 26
 *   directory inodes store one byte per child inode block starting at
 *          byte 32, so they never need data blocks
 *   data block: byte 2 links to the next data block of the file
 *   free block: byte 2 links to the next free block
 */

#define MAX_OPEN_FILES 32
#define MAX_FILENAME 8
#define NO_BLOCK 0

#define SUPER_ROOT 4

/* name field needs MAX_FILENAME + 1 bytes so an 8 char name keeps its NUL */
#define INODE_NAME_START 4
#define INODE_SIZE_START 16
#define INODE_DATA_START 20
#define INODE_RO_FLAG 24
#define INODE_DIR_FLAG 25
#define INODE_PARENT 26

#define DIR_ENTRIES_START 32

#define DATA_BYTES_PER_BLOCK 252

typedef struct {
    int inUse;
    int inodeBlock;
    int filePointer;
} OpenFileEntry;

static int mountedDisk = -1;
static int isMounted = 0;
static int rootBlock = -1;
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

static int isDirectory(char inode[BLOCKSIZE]) {
    return inode[INODE_DIR_FLAG] == 1;
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

/* searches a directory's entry table for a child with the given name,
   returns the child's inode block number */
static int findInDir(int dirBlock, char *name) {
    char dir[BLOCKSIZE];
    char inode[BLOCKSIZE];
    int child;

    if (readBlock(mountedDisk, dirBlock, dir) < 0) {
        return TFS_ERR_DISK;
    }

    for (int i = DIR_ENTRIES_START; i < BLOCKSIZE; i++) {
        child = (unsigned char)dir[i];

        if (child == NO_BLOCK) {
            continue;
        }

        if (readBlock(mountedDisk, child, inode) < 0) {
            return TFS_ERR_DISK;
        }

        if (strcmp(&inode[INODE_NAME_START], name) == 0) {
            return child;
        }
    }

    return TFS_ERR_GENERAL;
}

static int addDirEntry(int dirBlock, int childBlock) {
    char dir[BLOCKSIZE];

    if (readBlock(mountedDisk, dirBlock, dir) < 0) {
        return TFS_ERR_DISK;
    }

    for (int i = DIR_ENTRIES_START; i < BLOCKSIZE; i++) {
        if ((unsigned char)dir[i] == NO_BLOCK) {
            dir[i] = childBlock;

            if (writeBlock(mountedDisk, dirBlock, dir) < 0) {
                return TFS_ERR_DISK;
            }

            return TFS_SUCCESS;
        }
    }

    return TFS_ERR_NO_SPACE;
}

static int removeDirEntry(int dirBlock, int childBlock) {
    char dir[BLOCKSIZE];

    if (readBlock(mountedDisk, dirBlock, dir) < 0) {
        return TFS_ERR_DISK;
    }

    for (int i = DIR_ENTRIES_START; i < BLOCKSIZE; i++) {
        if ((unsigned char)dir[i] == childBlock) {
            dir[i] = NO_BLOCK;

            if (writeBlock(mountedDisk, dirBlock, dir) < 0) {
                return TFS_ERR_DISK;
            }

            return TFS_SUCCESS;
        }
    }

    return TFS_ERR_GENERAL;
}

static int dirIsEmpty(char inode[BLOCKSIZE]) {
    for (int i = DIR_ENTRIES_START; i < BLOCKSIZE; i++) {
        if ((unsigned char)inode[i] != NO_BLOCK) {
            return 0;
        }
    }

    return 1;
}

/* walks a "/" separated absolute path down from the root directory and
   returns the block of the directory that should hold the final
   component, which is copied into lastName; fails if any directory
   along the way is missing */
static int resolveParentDir(char *path, char *lastName) {
    char inode[BLOCKSIZE];
    char component[MAX_FILENAME + 1];
    int current;
    int next;
    int len;

    if (path == NULL) {
        return TFS_ERR_BAD_NAME;
    }

    if (*path == '/') {
        path++;
    }

    if (*path == '\0') {
        return TFS_ERR_BAD_NAME;
    }

    current = rootBlock;

    while (1) {
        len = 0;

        while (path[len] != '\0' && path[len] != '/') {
            if (len >= MAX_FILENAME) {
                return TFS_ERR_BAD_NAME;
            }

            component[len] = path[len];
            len++;
        }

        component[len] = '\0';

        if (!isValidName(component)) {
            return TFS_ERR_BAD_NAME;
        }

        if (path[len] == '\0') {
            strcpy(lastName, component);
            return current;
        }

        next = findInDir(current, component);

        if (next < 0) {
            return TFS_ERR_NO_DIR;
        }

        if (readBlock(mountedDisk, next, inode) < 0) {
            return TFS_ERR_DISK;
        }

        if (!isDirectory(inode)) {
            return TFS_ERR_NO_DIR;
        }

        current = next;
        path += len + 1;
    }
}

/* resolves a full path to an inode block number */
static int lookupPath(char *path) {
    char lastName[MAX_FILENAME + 1];
    int parentBlock;

    parentBlock = resolveParentDir(path, lastName);

    if (parentBlock < 0) {
        return parentBlock;
    }

    return findInDir(parentBlock, lastName);
}

/* allocates a fresh inode, links it into its parent directory and
   returns its block number */
static int createInode(int parentBlock, char *name, int isDir) {
    char inode[BLOCKSIZE];
    int inodeBlock;
    int result;

    inodeBlock = getFreeBlock();

    if (inodeBlock < 0) {
        return inodeBlock;
    }

    initBlock(inode, BLOCK_INODE);

    strcpy(&inode[INODE_NAME_START], name);
    setFileSize(inode, 0);
    setFirstDataBlock(inode, NO_BLOCK);
    setReadOnly(inode, 0);
    inode[INODE_DIR_FLAG] = isDir ? 1 : 0;
    inode[INODE_PARENT] = parentBlock;

    if (writeBlock(mountedDisk, inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    result = addDirEntry(parentBlock, inodeBlock);

    if (result < 0) {
        returnBlockToFreeList(inodeBlock);
        return result;
    }

    return inodeBlock;
}

/* closes every open descriptor that points at the given inode */
static void closeDescriptorsFor(int inodeBlock) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (openFiles[i].inUse && openFiles[i].inodeBlock == inodeBlock) {
            openFiles[i].inUse = 0;
            openFiles[i].inodeBlock = -1;
            openFiles[i].filePointer = 0;
        }
    }
}

/* recursively frees a file or directory tree rooted at inodeBlock */
static int removeTree(int inodeBlock) {
    char inode[BLOCKSIZE];
    int child;

    if (readBlock(mountedDisk, inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    if (isDirectory(inode)) {
        for (int i = DIR_ENTRIES_START; i < BLOCKSIZE; i++) {
            child = (unsigned char)inode[i];

            if (child != NO_BLOCK) {
                if (removeTree(child) < 0) {
                    return TFS_ERR_DISK;
                }
            }
        }
    } else {
        if (freeDataBlocks(getFirstDataBlock(inode)) < 0) {
            return TFS_ERR_DISK;
        }
    }

    closeDescriptorsFor(inodeBlock);

    return returnBlockToFreeList(inodeBlock);
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

    block[SUPER_ROOT] = 1;

    if (totalBlocks > 2) {
        block[BYTE_LINK] = 2;
    }

    if (writeBlock(disk, 0, block) < 0) {
        closeDisk(disk);
        return TFS_ERR_DISK;
    }

    /* block 1 is the root directory inode */
    initBlock(block, BLOCK_INODE);
    block[INODE_DIR_FLAG] = 1;

    if (writeBlock(disk, 1, block) < 0) {
        closeDisk(disk);
        return TFS_ERR_DISK;
    }

    for (int i = 2; i < totalBlocks; i++) {
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

    rootBlock = (unsigned char)block[SUPER_ROOT];

    if (rootBlock == NO_BLOCK ||
        readBlock(disk, rootBlock, block) < 0 ||
        (unsigned char)block[BYTE_TYPE] != BLOCK_INODE ||
        !isDirectory(block)) {
        closeDisk(disk);
        rootBlock = -1;
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
    rootBlock = -1;

    return TFS_SUCCESS;
}

/* name may be a plain file name (placed in the root directory) or a
   "/" separated absolute path; every directory in the path must exist */
fileDescriptor tfs_openFile(char *name) {
    char inode[BLOCKSIZE];
    char lastName[MAX_FILENAME + 1];
    int parentBlock;
    int inodeBlock;
    int fd;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    parentBlock = resolveParentDir(name, lastName);

    if (parentBlock < 0) {
        return parentBlock;
    }

    inodeBlock = findInDir(parentBlock, lastName);

    if (inodeBlock >= 0) {
        if (readBlock(mountedDisk, inodeBlock, inode) < 0) {
            return TFS_ERR_DISK;
        }

        if (isDirectory(inode)) {
            return TFS_ERR_IS_DIR;
        }
    } else {
        inodeBlock = createInode(parentBlock, lastName, 0);

        if (inodeBlock < 0) {
            return inodeBlock;
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

    if (removeDirEntry((unsigned char)inode[INODE_PARENT], inodeBlock) < 0) {
        return TFS_ERR_DISK;
    }

    if (returnBlockToFreeList(inodeBlock) < 0) {
        return TFS_ERR_DISK;
    }

    /* the same file may be open under several descriptors, so close
       every entry that points at the freed inode */
    closeDescriptorsFor(inodeBlock);

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

/* prints every entry under dirBlock with its absolute path, returns the
   number of entries printed */
static int printDirTree(int dirBlock, char *path) {
    char dir[BLOCKSIZE];
    char inode[BLOCKSIZE];
    char childPath[256];
    int child;
    int count = 0;

    if (readBlock(mountedDisk, dirBlock, dir) < 0) {
        return 0;
    }

    for (int i = DIR_ENTRIES_START; i < BLOCKSIZE; i++) {
        child = (unsigned char)dir[i];

        if (child == NO_BLOCK) {
            continue;
        }

        if (readBlock(mountedDisk, child, inode) < 0) {
            continue;
        }

        snprintf(childPath, sizeof(childPath), "%s/%s", path, &inode[INODE_NAME_START]);

        if (isDirectory(inode)) {
            printf("  %s/\n", childPath);
            count += 1 + printDirTree(child, childPath);
        } else {
            printf("  %s\n", childPath);
            count++;
        }
    }

    return count;
}

int tfs_readdir(void) {
    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    printf("Files on TinyFS disk:\n");

    if (printDirTree(rootBlock, "") == 0) {
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

    if (readBlock(mountedDisk, openFiles[FD].inodeBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    if ((unsigned char)inode[BYTE_TYPE] != BLOCK_INODE) {
        return TFS_ERR_BAD_FD;
    }

    /* the new name only has to be unique within the file's directory */
    if (findInDir((unsigned char)inode[INODE_PARENT], newName) >= 0) {
        return TFS_ERR_GENERAL;
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

    inodeBlock = lookupPath(name);

    if (inodeBlock < 0) {
        return inodeBlock;
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

    inodeBlock = lookupPath(name);

    if (inodeBlock < 0) {
        return inodeBlock;
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

/* creates a directory; every directory above it in the path must exist */
int tfs_createDir(char *dirName) {
    char lastName[MAX_FILENAME + 1];
    int parentBlock;
    int inodeBlock;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    parentBlock = resolveParentDir(dirName, lastName);

    if (parentBlock < 0) {
        return parentBlock;
    }

    if (findInDir(parentBlock, lastName) >= 0) {
        return TFS_ERR_GENERAL;
    }

    inodeBlock = createInode(parentBlock, lastName, 1);

    if (inodeBlock < 0) {
        return inodeBlock;
    }

    return TFS_SUCCESS;
}

/* removes a directory, which must be empty */
int tfs_removeDir(char *dirName) {
    char inode[BLOCKSIZE];
    char lastName[MAX_FILENAME + 1];
    int parentBlock;
    int dirBlock;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    parentBlock = resolveParentDir(dirName, lastName);

    if (parentBlock < 0) {
        return parentBlock;
    }

    dirBlock = findInDir(parentBlock, lastName);

    if (dirBlock < 0) {
        return TFS_ERR_NO_DIR;
    }

    if (readBlock(mountedDisk, dirBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    if (!isDirectory(inode)) {
        return TFS_ERR_NO_DIR;
    }

    if (!dirIsEmpty(inode)) {
        return TFS_ERR_NOT_EMPTY;
    }

    if (removeDirEntry(parentBlock, dirBlock) < 0) {
        return TFS_ERR_DISK;
    }

    return returnBlockToFreeList(dirBlock);
}

/* recursively removes a directory and everything below it; "/" empties
   the whole file system but keeps the root directory itself */
int tfs_removeAll(char *dirName) {
    char inode[BLOCKSIZE];
    char lastName[MAX_FILENAME + 1];
    int parentBlock;
    int dirBlock;
    int child;

    if (!isMounted) {
        return TFS_ERR_NOT_MOUNTED;
    }

    if (dirName != NULL && strcmp(dirName, "/") == 0) {
        if (readBlock(mountedDisk, rootBlock, inode) < 0) {
            return TFS_ERR_DISK;
        }

        for (int i = DIR_ENTRIES_START; i < BLOCKSIZE; i++) {
            child = (unsigned char)inode[i];

            if (child != NO_BLOCK) {
                if (removeTree(child) < 0) {
                    return TFS_ERR_DISK;
                }

                inode[i] = NO_BLOCK;
            }
        }

        if (writeBlock(mountedDisk, rootBlock, inode) < 0) {
            return TFS_ERR_DISK;
        }

        return TFS_SUCCESS;
    }

    parentBlock = resolveParentDir(dirName, lastName);

    if (parentBlock < 0) {
        return parentBlock;
    }

    dirBlock = findInDir(parentBlock, lastName);

    if (dirBlock < 0) {
        return TFS_ERR_NO_DIR;
    }

    if (readBlock(mountedDisk, dirBlock, inode) < 0) {
        return TFS_ERR_DISK;
    }

    if (!isDirectory(inode)) {
        return TFS_ERR_NO_DIR;
    }

    if (removeDirEntry(parentBlock, dirBlock) < 0) {
        return TFS_ERR_DISK;
    }

    return removeTree(dirBlock);
}