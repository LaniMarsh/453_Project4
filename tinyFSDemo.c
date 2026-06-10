#include <stdio.h>
#include <string.h>

#include "tinyFS.h"

static void readWholeFile(fileDescriptor fd) {
    char c;

    tfs_seek(fd, 0);

    while (tfs_readByte(fd, &c) == TFS_SUCCESS) {
        putchar(c);
    }

    putchar('\n');
}

int main(void) {
    int result;
    fileDescriptor fd;
    fileDescriptor bigFd;
    char message[] = "hello tiny file system";
    char newMessage[] = "new writable content";
    char bigBuffer[600];

    printf("Creating TinyFS disk...\n");
    result = tfs_mkfs(DEFAULT_DISK_NAME, DEFAULT_DISK_SIZE);
    if (result < 0) {
        printf("tfs_mkfs failed: %d\n", result);
        return 1;
    }

    printf("Mounting TinyFS disk...\n");
    result = tfs_mount(DEFAULT_DISK_NAME);
    if (result < 0) {
        printf("tfs_mount failed: %d\n", result);
        return 1;
    }

    printf("Opening file1...\n");
    fd = tfs_openFile("file1");
    if (fd < 0) {
        printf("tfs_openFile failed: %d\n", fd);
        return 1;
    }

    printf("Writing: %s\n", message);
    result = tfs_writeFile(fd, message, strlen(message));
    if (result < 0) {
        printf("tfs_writeFile failed: %d\n", result);
        return 1;
    }

    printf("Reading before rename: ");
    readWholeFile(fd);

    printf("Directory listing before rename:\n");
    tfs_readdir();

    printf("Renaming file1 to file2...\n");
    result = tfs_rename(fd, "file2");
    if (result < 0) {
        printf("tfs_rename failed: %d\n", result);
        return 1;
    }

    printf("Directory listing after rename:\n");
    tfs_readdir();

    printf("Reading after rename: ");
    readWholeFile(fd);

    printf("Making file2 read-only...\n");
    result = tfs_makeRO("file2");
    if (result < 0) {
        printf("tfs_makeRO failed: %d\n", result);
        return 1;
    }

    printf("Trying to write while read-only...\n");
    result = tfs_writeFile(fd, newMessage, strlen(newMessage));
    printf("Write result while read-only should be negative: %d\n", result);

    printf("Trying to delete while read-only...\n");
    result = tfs_deleteFile(fd);
    printf("Delete result while read-only should be negative: %d\n", result);

    printf("Making file2 read-write again...\n");
    result = tfs_makeRW("file2");
    if (result < 0) {
        printf("tfs_makeRW failed: %d\n", result);
        return 1;
    }

    printf("Writing after making read-write: %s\n", newMessage);
    result = tfs_writeFile(fd, newMessage, strlen(newMessage));
    if (result < 0) {
        printf("tfs_writeFile after RW failed: %d\n", result);
        return 1;
    }

    printf("Reading after RW write: ");
    readWholeFile(fd);

    printf("Deleting file2...\n");
    result = tfs_deleteFile(fd);
    if (result < 0) {
        printf("tfs_deleteFile failed: %d\n", result);
        return 1;
    }

    printf("Reopening file1 after delete...\n");
    fd = tfs_openFile("file1");
    if (fd < 0) {
        printf("tfs_openFile failed after delete: %d\n", fd);
        return 1;
    }

    printf("Reading new file1 should be empty: ");
    readWholeFile(fd);

    printf("Writing to file1 again: %s\n", message);
    result = tfs_writeFile(fd, message, strlen(message));
    if (result < 0) {
        printf("tfs_writeFile failed: %d\n", result);
        return 1;
    }

    printf("Overwriting first byte of file1 with 'H' via tfs_writeByte...\n");
    tfs_seek(fd, 0);
    result = tfs_writeByte(fd, 'H');
    if (result < 0) {
        printf("tfs_writeByte failed: %d\n", result);
        return 1;
    }

    printf("Reading after writeByte: ");
    readWholeFile(fd);

    printf("Making file1 read-only and trying tfs_writeByte...\n");
    tfs_makeRO("file1");
    tfs_seek(fd, 0);
    result = tfs_writeByte(fd, 'x');
    printf("writeByte result while read-only should be negative: %d\n", result);
    tfs_makeRW("file1");

    printf("Writing a 600 byte file to show multi-block chaining...\n");
    for (int i = 0; i < 600; i++) {
        bigBuffer[i] = 'a' + (i % 26);
    }

    bigFd = tfs_openFile("bigfile");
    if (bigFd < 0) {
        printf("tfs_openFile failed for bigfile: %d\n", bigFd);
        return 1;
    }

    result = tfs_writeFile(bigFd, bigBuffer, 600);
    if (result < 0) {
        printf("tfs_writeFile failed for bigfile: %d\n", result);
        return 1;
    }

    printf("Reading bigfile back:\n");
    readWholeFile(bigFd);

    printf("Opening file with the longest legal name (8 chars)...\n");
    result = tfs_openFile("file5678");
    if (result < 0) {
        printf("tfs_openFile failed for file5678: %d\n", result);
        return 1;
    }
    tfs_closeFile(result);

    printf("Directory listing at the end:\n");
    tfs_readdir();

    tfs_closeFile(bigFd);

    result = tfs_closeFile(fd);
    if (result < 0) {
        printf("tfs_closeFile failed: %d\n", result);
        return 1;
    }

    result = tfs_unmount();
    if (result < 0) {
        printf("tfs_unmount failed: %d\n", result);
        return 1;
    }

    printf("TinyFS unmounted successfully.\n");

    return 0;
}