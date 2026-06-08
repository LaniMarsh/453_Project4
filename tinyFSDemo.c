#include <stdio.h>

#include "tinyFS.h"

int main(void) {
    int result;
    fileDescriptor fd;

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

    printf("TinyFS mounted successfully.\n");

    printf("Opening file named file1...\n");

    fd = tfs_openFile("file1");
    if (fd < 0) {
        printf("tfs_openFile failed: %d\n", fd);
        return 1;
    }

    printf("file1 opened with FD %d\n", fd);

    result = tfs_closeFile(fd);
    if (result < 0) {
        printf("tfs_closeFile failed: %d\n", result);
        return 1;
    }

    printf("file1 closed successfully.\n");

    printf("Unmounting TinyFS disk...\n");

    result = tfs_unmount();
    if (result < 0) {
        printf("tfs_unmount failed: %d\n", result);
        return 1;
    }

    printf("TinyFS unmounted successfully.\n");

    return 0;
}