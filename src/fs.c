#include "fs.h"

/* On-disk layout (block size 4096 bytes):
 *   block 0      superblock
 *   block 1      free-block bitmap (1 bit per block)
 *   blocks 2-9   inode table
 *   blocks 10+   file data
 */
#define METADATA_BLOCKS 10
#define BITMAP_OFFSET (1 * BLOCK_SIZE)
#define INODE_TABLE_OFFSET (2 * BLOCK_SIZE)

/* In-memory copies of the filesystem metadata. */
static superblock sb;
static unsigned char bitmap[MAX_BLOCKS / 8];
static inode inodes[MAX_FILES];
static int fd = -1;

/* ---------------------- Internal helpers ---------------------- */
/* Return the index of the in-use inode named filename, or -1. */
static int find_inode(const char *filename)
{
    for (int i = 0; i < MAX_FILES; i++)
    {
        if (inodes[i].used && strcmp(inodes[i].name, filename) == 0)
        {
            return i;
        }
    }
    return -1;
}
/* Return the index of an unused inode, or -1. */
static int find_free_inode(void)
{
    for (int i = 0; i < MAX_FILES; i++)
    {
        if (inodes[i].used == 0)
        {
            return i;
        }
    }
    return -1;
}

/* Return the first block whose bitmap bit is clear, or -1. */
static int find_free_block(void)
{
    for (int i = 0; i < MAX_BLOCKS; i++)
    {
        int byte_index = i / 8;
        int bit_index = i % 8;
        if ((bitmap[byte_index] & (1 << bit_index)) == 0)
        {
            return i;
        }
    }
    return -1;
}
/* Bitmap helpers: set / clear the bit for block_num. */
static void mark_block_used(int block_num)
{
    int byte_index = block_num / 8;
    int bit_index = block_num % 8;
    bitmap[byte_index] |= (1 << bit_index);
}

static void mark_block_free(int block_num)
{
    int byte_index = block_num / 8;
    int bit_index = block_num % 8;
    bitmap[byte_index] &= ~(1 << bit_index);
}
/**
 * @brief Pushes the current in-memory superblock, bitmap, and inode table to disk.
 * @return 0 on success; -1 on failure.
 */
static int push_changes_to_disk(void)
{
    lseek(fd, 0, SEEK_SET);
    int res = write(fd, &sb, sizeof(superblock));
    if (res != sizeof(superblock)) {
        return -1;
    }

    lseek(fd, BITMAP_OFFSET, SEEK_SET);
    res = write(fd, bitmap, sizeof(bitmap));
    if (res != sizeof(bitmap)) {
        return -1;
    }

    lseek(fd, INODE_TABLE_OFFSET, SEEK_SET);
    res = write(fd, inodes, sizeof(inodes));
    if (res != sizeof(inodes)) {
        return -1;
    }
    return 0;
}
/**
 * @brief Creates and initializes a virtual disk file.
 *
 * If the file already exists, it is overwritten. Creates a 10 MB file and initializes
 * the superblock, bitmap, and inode table. All blocks except metadata blocks are marked free.
 *
 * @param disk_path Path where the virtual disk file will be created.
 * @return 0 on success; -1 on failure.
 */
int fs_format(const char *disk_path)
{
    sb.total_blocks = MAX_BLOCKS;
    sb.block_size = BLOCK_SIZE;
    sb.free_blocks = MAX_BLOCKS - METADATA_BLOCKS;
    sb.total_inodes = MAX_FILES;
    sb.free_inodes = MAX_FILES;

    if(disk_path == NULL)
    {
        fprintf(stderr, "Error: disk_path is NULL in fs_format.\n");
        return -1;
    }

    fd = open(disk_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        perror("Error opening disk file in fs_format");
        return -1;
    }

    lseek(fd, 0, SEEK_SET);
    if (write(fd, &sb, sizeof(superblock)) != sizeof(superblock))
    {
        perror("Error writing superblock in fs_format");
        close(fd);
        fd = -1;
        return -1;
    }
    for (size_t i = 0; i < sizeof(bitmap); i++)
    {
        bitmap[i] = 0;
    }
    for (int i = 0; i < METADATA_BLOCKS; i++)
    {
        mark_block_used(i);
    }
    lseek(fd, BITMAP_OFFSET, SEEK_SET);
    if (write(fd, bitmap, sizeof(bitmap)) != sizeof(bitmap))
    {
        perror("Error writing bitmap in fs_format");
        close(fd);
        fd = -1;
        return -1;
    }
    for (int i = 0; i < MAX_FILES; i++)
    {
        inodes[i].used = 0;
        inodes[i].name[0] = '\0';
        inodes[i].size = 0;
        for (int j = 0; j < MAX_DIRECT_BLOCKS; j++) {
            inodes[i].blocks[j] = -1;
        }
    }
    lseek(fd, INODE_TABLE_OFFSET, SEEK_SET);
    if (write(fd, inodes, sizeof(inodes)) != sizeof(inodes))
    {
        perror("Error writing inode table in fs_format");
        close(fd);
        fd = -1;
        return -1;
    }
    lseek(fd, MAX_BLOCKS * BLOCK_SIZE - 1, SEEK_SET);
    if (write(fd, "\0", 1) != 1) {
        perror("Error setting disk file size in fs_format");
        close(fd);
        fd = -1;
        return -1;
    }
    close(fd);
    fd = -1;
    return 0;
}

/**
 * @brief Loads an existing filesystem from a virtual disk file.
 *
 * Opens the virtual disk file, reads and validates the superblock,
 * and loads necessary metadata into memory.
 *
 * @param disk_path Path to the virtual disk file.
 * @return 0 on success; -1 on failure.
 */
int fs_mount(const char *disk_path)
{
    if (disk_path == NULL) {
        fprintf(stderr, "Error: disk_path is NULL in fs_mount.\n");
        return -1;
    }
    fd = open(disk_path, O_RDWR);
    if (fd < 0)
    {
        perror("Error opening disk file in fs_mount");
        return -1;
    }
    if (read(fd, &sb, sizeof(superblock)) != sizeof(superblock)) {
        perror("Error reading superblock in fs_mount");
        goto fail;
    }
    if (sb.total_blocks != MAX_BLOCKS || sb.block_size != BLOCK_SIZE || sb.total_inodes != MAX_FILES) {
        fprintf(stderr, "Error: Invalid superblock values in fs_mount.\n");
        goto fail;
    }
    lseek(fd, BITMAP_OFFSET, SEEK_SET);
    if (read(fd, bitmap, sizeof(bitmap)) != sizeof(bitmap)) {
        perror("Error reading bitmap in fs_mount");
        goto fail;
    }
    lseek(fd, INODE_TABLE_OFFSET, SEEK_SET);
    if (read(fd, inodes, sizeof(inodes)) != sizeof(inodes)) {
        perror("Error reading inode table in fs_mount");
        goto fail;
    }
    return 0;

fail:
    close(fd);
    fd = -1;
    return -1;
}

/**
 * @brief Unmounts the filesystem.
 *
 * Flushes any cached data to disk and closes the virtual disk file.
 */
void fs_unmount(void)
{
    if (fd < 0) {
        fprintf(stderr, "Error: No filesystem is currently mounted in fs_unmount.\n");
        return;
    }
    int res = push_changes_to_disk();
    if (res < 0) {
        fprintf(stderr, "Error pushing changes to disk in fs_unmount.\n");
    }
    close(fd);
    fd = -1;
    return;
}

/**
 * @brief Creates a new empty file in the filesystem.
 *
 * @param filename Null-terminated string (max 28 chars excluding null).
 * @return 0 on success; -1 if file already exists; -2 if no free inodes; -3 for other errors.
 */
int fs_create(const char *filename)
{
    if (fd < 0 || filename == NULL || strlen(filename) >= MAX_FILENAME) {
        return -3;
    }
    if (find_inode(filename) >= 0)
    {
        return -1;
    }
    int free_inode_index = find_free_inode();
    if (free_inode_index < 0)
    {
        return -2; // No free inodes
    }
    inode *new_inode = &inodes[free_inode_index];
    new_inode->used = 1;
    strncpy(new_inode->name, filename, MAX_FILENAME - 1);
    new_inode->name[MAX_FILENAME - 1] = '\0'; // Ensure null-termination
    new_inode->size = 0;
    for (int i = 0; i < MAX_DIRECT_BLOCKS; i++)
    {
        new_inode->blocks[i] = -1; // No blocks allocated yet
    }
    sb.free_inodes--;
    int res = push_changes_to_disk();
    if (res < 0) {
        return -3;
    }
    return 0;
}

/**
 * @brief Lists files in the filesystem.
 *
 * @param filenames Pre-allocated 2D array to receive filenames.
 * @param max_files Maximum number of filenames to retrieve.
 * @return Number of files found (0 to max_files); -1 on error.
 */
int fs_list(char filenames[][MAX_FILENAME], int max_files)
{
    if (fd < 0 || max_files < 0 || filenames == NULL) {
        return -1;
    }
    int count = 0;
    for (int i = 0; i < MAX_FILES && count < max_files; i++)
    {
        if (inodes[i].used)
        {
            strncpy(filenames[count], inodes[i].name, MAX_FILENAME);
            count++;
        }
    }
    return count;
}

/**
 * @brief Writes data to a file, overwriting any existing content.
 *
 * @param filename Target file.
 * @param data Pointer to data buffer.
 * @param size Number of bytes to write.
 * @return 0 on success; -1 if file does not exist; -2 if out of space; -3 for other errors.
 */
int fs_write(const char *filename, const void *data, int size)
{
    if (filename == NULL) {
        return -3;
    }
    int inode_idx = find_inode(filename);
    if (inode_idx < 0) {
        return -1;
    }
    if (fd < 0 || data == NULL || size < 0) {
        return -3;
    }
    inode *inode = &inodes[inode_idx];
    int blocks_used = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (blocks_needed > MAX_DIRECT_BLOCKS) {
        return -3;
    }
    if (blocks_needed > (sb.free_blocks + blocks_used)) {
        return -2;
    }
    for (int i = 0; i < blocks_used; i++)
    {
        if (inode->blocks[i] >= 0)
        {
            mark_block_free(inode->blocks[i]);
            inode->blocks[i] = -1;
        }
    }
    for (int i = 0; i < blocks_needed; i++)
    {
        int free_block = find_free_block();
        if (free_block < 0) {
            return -2;
        }
        mark_block_used(free_block);
        inode->blocks[i] = free_block;
        lseek(fd, free_block * BLOCK_SIZE, SEEK_SET);
        int bytes_to_write = (size - i * BLOCK_SIZE) < BLOCK_SIZE ? (size - i * BLOCK_SIZE) : BLOCK_SIZE;
        if (write(fd, (char *)data + i * BLOCK_SIZE, bytes_to_write) != bytes_to_write)
        {
            return -3;
        }
    }
    inode->size = size;
    sb.free_blocks = sb.free_blocks - blocks_needed + blocks_used;

    // push changes to disk
    int res = push_changes_to_disk();
    if (res < 0) {
        return -3;
    }
    return 0;
}

/**
 * @brief Reads file content into a buffer.
 *
 * @param filename Source file.
 * @param buffer Pre-allocated buffer to receive data.
 * @param size Buffer size in bytes.
 * @return Number of bytes read on success; -1 if file does not exist; -3 for other errors.
 */
int fs_read(const char *filename, void *buffer, int size)
{
    if (filename == NULL) {
        return -3;
    }
    int inode_idx = find_inode(filename);
    if (inode_idx < 0) {
        return -1;
    }
    if (fd < 0 || buffer == NULL || size < 0) {
        return -3;
    }
    inode *file_inode = &inodes[inode_idx];
    int bytes_to_read = file_inode->size < size ? file_inode->size : size;
    if (bytes_to_read <= 0) {
        return 0;
    }
    int amount_read = 0;
    for (int i = 0; i < bytes_to_read / BLOCK_SIZE + 1 && i < MAX_DIRECT_BLOCKS; i++)
    {
        int bytes_left_to_read = bytes_to_read - amount_read;
        if (bytes_left_to_read <= 0) {
            break;
        }
        int physical_block = file_inode->blocks[i];
        if (physical_block < 0) {
            break;
        }
        lseek(fd, physical_block * BLOCK_SIZE, SEEK_SET);
        int bytes_this_read = bytes_left_to_read < BLOCK_SIZE ? bytes_left_to_read : BLOCK_SIZE;
        int res = read(fd, (char *)buffer + amount_read, bytes_this_read);
        if (res < 0) {
            return -3;
        }
        amount_read += bytes_this_read;
    }
    return amount_read;
}

/**
 * @brief Removes a file and frees its blocks.
 *
 * @param filename Null-terminated string.
 * @return 0 on success; -1 if file does not exist; -2 for other errors.
 */
int fs_delete(const char *filename)
{
    // checks before proceeding
    if (fd < 0 || filename == NULL || strlen(filename) >= MAX_FILENAME) {
        return -2;
    }
    // find the file
    int file = find_inode(filename);
    if (file < 0) {
        return -1; // file not found
    }
    inode *inode = &inodes[file];
    int used_blocks = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (int i = 0; i < MAX_DIRECT_BLOCKS; i++) {
        if (inode->blocks[i] != -1) {
            // Marks all of the file's blocks as free in the bitmap.
            mark_block_free(inode->blocks[i]);
            inode->blocks[i] = -1;
        }
    }
    // Marks the inode as free
    inode->used = 0;
    inode->name[0] = '\0';
    inode->size = 0;

    // updates the superblock freed inode and blocks.
    sb.free_inodes++;
    sb.free_blocks += used_blocks;

    // push changes to disk
    int res = push_changes_to_disk();
    if (res < 0) {
        return -2;
    }
    return 0;
}