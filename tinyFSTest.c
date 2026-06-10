#include <stdio.h>
#include <string.h>

#include "tinyFS.h"

#define TEST_DISK "testDisk"
#define JUNK_DISK "junkDisk"
#define SMALL_DISK "smallDsk"

static int testsRun = 0;
static int testsFailed = 0;

static void check(int condition, char *label) {
    testsRun++;

    if (condition) {
        printf("PASS: %s\n", label);
    } else {
        printf("FAIL: %s\n", label);
        testsFailed++;
    }
}

/* reads from the start of the file until EOF, returns the byte count */
static int readAll(fileDescriptor fd, char *buffer, int max) {
    int count = 0;
    char c;

    tfs_seek(fd, 0);

    while (count < max && tfs_readByte(fd, &c) == TFS_SUCCESS) {
        buffer[count] = c;
        count++;
    }

    return count;
}

int main(void) {
    char bigBuffer[2048];
    char readBuffer[2048];
    char blockBuffer[BLOCKSIZE];
    char c;
    int count;
    int disk;
    fileDescriptor fd;
    fileDescriptor fd2;

    for (int i = 0; i < 2048; i++) {
        bigBuffer[i] = 'a' + (i % 26);
    }

    /* operations before anything is mounted */
    check(tfs_openFile("file1") == TFS_ERR_NOT_MOUNTED, "openFile fails when unmounted");
    check(tfs_readdir() < 0, "readdir fails when unmounted");
    check(tfs_writeByte(0, 'a') < 0, "writeByte fails when unmounted");
    check(tfs_unmount() < 0, "unmount fails when nothing mounted");

    /* mkfs size limits */
    check(tfs_mkfs(TEST_DISK, 100) < 0, "mkfs rejects disk smaller than two blocks");
    check(tfs_mkfs(TEST_DISK, 300 * BLOCKSIZE) < 0, "mkfs rejects disk over 256 blocks");

    /* raw disk layer checks */
    disk = openDisk(JUNK_DISK, 1024);
    check(disk >= 0, "openDisk creates a new disk");
    check(readBlock(disk, 3, blockBuffer) == 0, "readBlock accepts last valid block");
    check(readBlock(disk, 4, blockBuffer) < 0, "readBlock rejects block past disk end");
    check(readBlock(disk, -1, blockBuffer) < 0, "readBlock rejects negative block");
    check(writeBlock(disk, 4, blockBuffer) < 0, "writeBlock rejects block past disk end");
    check(readBlock(99, 0, blockBuffer) < 0, "readBlock rejects bad disk number");
    check(closeDisk(disk) == 0, "closeDisk succeeds");
    check(closeDisk(disk) < 0, "closeDisk fails on already closed disk");

    /* the junk disk is all zeros, so it has no magic number */
    check(tfs_mount(JUNK_DISK) == TFS_ERR_BAD_FS, "mount rejects non TinyFS disk");

    /* make and mount a real file system */
    check(tfs_mkfs(TEST_DISK, DEFAULT_DISK_SIZE) == TFS_SUCCESS, "mkfs succeeds");
    check(tfs_mount(TEST_DISK) == TFS_SUCCESS, "mount succeeds");
    check(tfs_mount(TEST_DISK) == TFS_ERR_ALREADY_MOUNTED, "second mount fails");

    /* multi-block write and full read back */
    fd = tfs_openFile("big");
    check(fd >= 0, "openFile creates big file");
    check(tfs_writeFile(fd, bigBuffer, 600) == TFS_SUCCESS, "600 byte write succeeds");

    count = readAll(fd, readBuffer, 2048);
    check(count == 600, "read back exactly 600 bytes");
    check(memcmp(bigBuffer, readBuffer, 600) == 0, "multi-block content matches");

    /* readByte at EOF must not advance the pointer */
    check(tfs_readByte(fd, &c) == TFS_ERR_EOF, "readByte at EOF returns error");
    check(tfs_readByte(fd, &c) == TFS_ERR_EOF, "readByte at EOF stays at EOF");
    check(tfs_seek(fd, 599) == TFS_SUCCESS, "seek to last byte succeeds");
    check(tfs_readByte(fd, &c) == TFS_SUCCESS && c == bigBuffer[599], "last byte readable after EOF error");

    /* overwriting with a smaller file shrinks it */
    check(tfs_writeFile(fd, "tiny", 4) == TFS_SUCCESS, "overwrite with 4 bytes succeeds");
    count = readAll(fd, readBuffer, 2048);
    check(count == 4 && memcmp(readBuffer, "tiny", 4) == 0, "file shrank to new content");

    /* seek edge cases */
    check(tfs_seek(fd, -1) < 0, "seek rejects negative offset");
    check(tfs_seek(fd, 1000) == TFS_SUCCESS, "seek past EOF is allowed");
    check(tfs_readByte(fd, &c) == TFS_ERR_EOF, "readByte past EOF returns error");

    /* bad file descriptors */
    check(tfs_readByte(-1, &c) == TFS_ERR_BAD_FD, "readByte rejects negative FD");
    check(tfs_readByte(99, &c) == TFS_ERR_BAD_FD, "readByte rejects out of range FD");
    check(tfs_seek(20, 0) == TFS_ERR_BAD_FD, "seek rejects unopened FD");
    check(tfs_closeFile(20) == TFS_ERR_BAD_FD, "closeFile rejects unopened FD");
    check(tfs_writeFile(20, "x", 1) == TFS_ERR_BAD_FD, "writeFile rejects unopened FD");
    check(tfs_openFile("waytoolongname") == TFS_ERR_BAD_NAME, "openFile rejects long name");
    check(tfs_openFile("bad name") == TFS_ERR_BAD_NAME, "openFile rejects non alphanumeric name");

    /* full length (8 char) file names */
    fd = tfs_openFile("file5678");
    check(fd >= 0, "openFile creates 8 char name");
    check(tfs_writeFile(fd, "ABCDEFGHIJ", 10) == TFS_SUCCESS, "write to 8 char name succeeds");

    fd2 = tfs_openFile("file5678");
    check(fd2 >= 0, "reopening 8 char name succeeds");
    count = readAll(fd2, readBuffer, 2048);
    check(count == 10 && readBuffer[0] == 'A', "reopen found the same file, not a new one");
    tfs_closeFile(fd2);

    check(tfs_rename(fd, "renamed8") == TFS_SUCCESS, "rename to 8 char name succeeds");
    count = readAll(fd, readBuffer, 2048);
    check(count == 10, "file size survives rename");

    fd2 = tfs_openFile("renamed8");
    check(fd2 >= 0, "renamed file findable by new name");
    tfs_closeFile(fd2);

    /* writeByte */
    tfs_seek(fd, 0);
    check(tfs_writeByte(fd, 'Z') == TFS_SUCCESS, "writeByte succeeds");
    tfs_seek(fd, 0);
    check(tfs_readByte(fd, &c) == TFS_SUCCESS && c == 'Z', "writeByte changed the byte");
    tfs_seek(fd, 10);
    check(tfs_writeByte(fd, 'Z') == TFS_ERR_EOF, "writeByte at EOF returns error");

    check(tfs_makeRO("renamed8") == TFS_SUCCESS, "makeRO succeeds");
    tfs_seek(fd, 0);
    check(tfs_writeByte(fd, 'x') == TFS_ERR_READ_ONLY, "writeByte fails on read-only file");
    check(tfs_writeFile(fd, "x", 1) == TFS_ERR_READ_ONLY, "writeFile fails on read-only file");
    check(tfs_deleteFile(fd) == TFS_ERR_READ_ONLY, "deleteFile fails on read-only file");
    check(tfs_makeRW("renamed8") == TFS_SUCCESS, "makeRW succeeds");
    tfs_closeFile(fd);

    /* deleting a file invalidates every descriptor that points at it */
    fd = tfs_openFile("dup");
    fd2 = tfs_openFile("dup");
    check(fd >= 0 && fd2 >= 0 && fd != fd2, "same file opened under two FDs");
    check(tfs_writeFile(fd, "abc", 3) == TFS_SUCCESS, "write through first FD succeeds");
    check(tfs_deleteFile(fd) == TFS_SUCCESS, "delete through first FD succeeds");
    check(tfs_writeFile(fd2, "abc", 3) == TFS_ERR_BAD_FD, "stale FD cannot write after delete");
    check(tfs_readByte(fd2, &c) == TFS_ERR_BAD_FD, "stale FD cannot read after delete");

    /* content survives unmount and remount */
    fd = tfs_openFile("persist");
    tfs_writeFile(fd, "persistent data", 15);
    tfs_closeFile(fd);
    check(tfs_unmount() == TFS_SUCCESS, "unmount succeeds");
    check(tfs_mount(TEST_DISK) == TFS_SUCCESS, "remount succeeds");
    fd = tfs_openFile("persist");
    count = readAll(fd, readBuffer, 2048);
    check(count == 15 && memcmp(readBuffer, "persistent data", 15) == 0, "content survived remount");
    tfs_closeFile(fd);
    tfs_unmount();

    /* running out of space must not corrupt the file system */
    check(tfs_mkfs(SMALL_DISK, 8 * BLOCKSIZE) == TFS_SUCCESS, "mkfs on tiny 8 block disk");
    check(tfs_mount(SMALL_DISK) == TFS_SUCCESS, "mount tiny disk");

    fd = tfs_openFile("full1");
    check(fd >= 0, "openFile on tiny disk succeeds");
    check(tfs_writeFile(fd, bigBuffer, 2000) == TFS_ERR_NO_SPACE, "oversized write fails with no space");
    check(tfs_writeFile(fd, "hello", 5) == TFS_SUCCESS, "small write still works after failed write");
    count = readAll(fd, readBuffer, 2048);
    check(count == 5 && memcmp(readBuffer, "hello", 5) == 0, "small file readable after failed write");

    /* fill the disk completely, then free it again */
    check(tfs_writeFile(fd, bigBuffer, 1500) == TFS_SUCCESS, "write filling every free block succeeds");
    check(tfs_openFile("full2") == TFS_ERR_NO_SPACE, "openFile fails when disk is full");
    check(tfs_deleteFile(fd) == TFS_SUCCESS, "delete frees the blocks");
    fd2 = tfs_openFile("full2");
    check(fd2 >= 0, "openFile works again after delete");
    check(tfs_writeFile(fd2, bigBuffer, 100) == TFS_SUCCESS, "write works again after delete");
    tfs_closeFile(fd2);
    tfs_unmount();

    printf("\n%d tests run, %d failed\n", testsRun, testsFailed);

    return testsFailed != 0;
}
