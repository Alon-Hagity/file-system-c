# Simple File System in C

A small inode-based file system that lives inside a single 10 MB "virtual disk" file. It supports formatting, mounting, creating, writing, reading, listing and deleting files, and keeps all metadata on disk so data survives an unmount/remount cycle.

## Design

### Disk layout

The virtual disk is 2,560 blocks of 4,096 bytes (10 MB):

| Blocks | Contents |
|---|---|
| 0 | Superblock: total/free block counts, block size, total/free inode counts |
| 1 | Free-block bitmap, one bit per block (320 bytes used) |
| 2-9 | Inode table: 256 inodes x 88 bytes = 22,528 bytes (fits in 6 blocks; 8 are reserved) |
| 10-2559 | File data |

Blocks 0-9 are marked as used in the bitmap at format time, so data allocation always starts at block 10.

### Inodes and blocks

Each inode stores an in-use flag, a file name (up to 28 characters), the file size in bytes, and 12 direct block pointers (`-1` = unused). There are no directories (one flat namespace) and no indirect blocks, so the maximum file size is 12 x 4 KB = 48 KB. Free blocks are found with a first-fit scan of the bitmap.

### Operations

| Function | Behavior |
|---|---|
| `fs_format(path)` | Creates or truncates the disk file, writes a fresh superblock, bitmap and inode table, and extends the file to 10 MB |
| `fs_mount(path)` | Opens the disk, validates the superblock, and loads the bitmap and inode table into memory |
| `fs_unmount()` | Writes the metadata back to disk and closes the file |
| `fs_create(name)` | Claims a free inode for an empty file. Returns -1 if the name exists, -2 if there are no free inodes |
| `fs_write(name, data, size)` | Overwrites the file: frees its old blocks, allocates new ones and writes the data block by block. Returns -2 if out of space |
| `fs_read(name, buf, size)` | Reads up to `min(size, file size)` bytes and returns the byte count |
| `fs_delete(name)` | Frees the file's blocks and inode |
| `fs_list(names, max)` | Copies up to `max` file names and returns how many were copied |

Metadata lives in memory while the disk is mounted. It is written back (write-through) after every `create`, `write` and `delete`, and again on unmount.

## Build and run

Requires `gcc` and `make` on Linux or macOS.

```bash
make        # builds build/libsimplefs.a and build/fs_demo
make run    # runs the demo (creates demo_disk.img in the current directory)
make clean
```

To use the library in your own program, include `include/fs.h` and link with `-Lbuild -lsimplefs`.

## Example session

```
$ make run
Formatted and mounted demo_disk.img
create same name again -> -1 (expected -1)
After creating files:
  3 file(s): hello.txt alphabet.bin empty.txt
Unmounted; remounting to check persistence...
read hello.txt (43 bytes): Hello from a tiny inode-based file system!
read alphabet.bin: 12388 bytes, contents match
delete empty.txt -> 0
read missing file -> -1 (expected -1)
Final listing:
  2 file(s): hello.txt alphabet.bin
```

## Project layout

```
include/fs.h     public API, constants and on-disk structures
src/fs.c         implementation
examples/demo.c  end-to-end demo
Makefile
```

## Authors

- Alon Hagity ([LinkedIn](https://www.linkedin.com/in/alon-hagity/))

<sub>Credits: developed with Nicole Yerushalmi · Operating Systems, Reichman University, Spring 2026.</sub>
