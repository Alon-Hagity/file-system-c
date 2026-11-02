/*
 * Demo: format a virtual disk, create and write files, remount, read back,
 * delete a file and list what remains.
 */
#include "fs.h"

#define DISK_PATH "demo_disk.img"

static void list_files(void)
{
    char names[MAX_FILES][MAX_FILENAME];
    int n = fs_list(names, MAX_FILES);
    printf("  %d file(s):", n);
    for (int i = 0; i < n; i++)
        printf(" %s", names[i]);
    printf("\n");
}

int main(void)
{
    if (fs_format(DISK_PATH) != 0 || fs_mount(DISK_PATH) != 0) {
        fprintf(stderr, "could not format/mount %s\n", DISK_PATH);
        return 1;
    }
    printf("Formatted and mounted %s\n", DISK_PATH);

    const char *greeting = "Hello from a tiny inode-based file system!";
    fs_create("hello.txt");
    fs_write("hello.txt", greeting, (int)strlen(greeting) + 1);

    /* A multi-block file: 3 blocks + 100 bytes. */
    int big_size = 3 * BLOCK_SIZE + 100;
    char *big = malloc(big_size);
    for (int i = 0; i < big_size; i++)
        big[i] = (char)('a' + i % 26);
    fs_create("alphabet.bin");
    fs_write("alphabet.bin", big, big_size);
    fs_create("empty.txt");

    printf("create same name again -> %d (expected -1)\n", fs_create("hello.txt"));
    printf("After creating files:\n");
    list_files();

    fs_unmount();
    printf("Unmounted; remounting to check persistence...\n");
    fs_mount(DISK_PATH);

    char buf[64];
    int n = fs_read("hello.txt", buf, sizeof(buf));
    printf("read hello.txt (%d bytes): %s\n", n, buf);

    char *check = malloc(big_size);
    n = fs_read("alphabet.bin", check, big_size);
    printf("read alphabet.bin: %d bytes, contents %s\n", n,
           (n == big_size && memcmp(big, check, big_size) == 0) ? "match" : "MISMATCH");

    printf("delete empty.txt -> %d\n", fs_delete("empty.txt"));
    printf("read missing file -> %d (expected -1)\n", fs_read("nope.txt", buf, sizeof(buf)));
    printf("Final listing:\n");
    list_files();

    fs_unmount();
    free(big);
    free(check);
    return 0;
}
