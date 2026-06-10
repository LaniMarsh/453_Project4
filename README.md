# 453_Project4

## TinyFS and disk emulator
Contributors: Joaquin Arredondo, Alanis Marsh, and Abhiram Yakka

## Additional features

### Directory listing and file renaming
`tfs_rename(FD, newName)` renames an open file in place; the new name only
has to be unique within the file's directory. `tfs_readdir()` walks the
directory tree from the root and prints every file and directory with its
absolute path.

### Read-only and writeByte support
`tfs_makeRO(name)` / `tfs_makeRW(name)` toggle a read-only flag stored in the
inode. While a file is read-only, `tfs_writeFile()`, `tfs_deleteFile()` and
`tfs_writeByte()` fail with `TFS_ERR_READ_ONLY`. `tfs_writeByte(FD, data)`
overwrites one byte at the current file pointer and advances it; it cannot
grow a file past its existing size.

### Hierarchical directories: design and implementation
The superblock stores the block number of the root directory inode at byte 4;
`tfs_mkfs` creates the root at block 1. An inode has a directory flag at byte
25 that marks it as a directory instead of a file, and byte 26 stores the
block number of its parent directory, which lets delete and rename find the
containing directory without re-walking the path.

Since the file system is capped at 256 blocks, a block number always fits in
one byte. A directory therefore keeps its contents directly inside its inode
block as a table of child inode block numbers (bytes 32-255, zero meaning an
empty slot), so directories never need data blocks and can hold up to 224
entries. A lookup reads the directory's table and compares each child's name;
a path like `/docs/notes/memo` is resolved one component at a time starting
from the root, and fails with `TFS_ERR_NO_DIR` as soon as a component is
missing or is not a directory.

All paths are absolute; the leading `/` is optional, so a bare name like
`file1` keeps its old meaning of a file in the root directory, which keeps
`tfs_openFile()` backwards compatible. `tfs_createDir` and `tfs_removeDir`
create and remove a single (empty) directory, and `tfs_removeAll` recursively
frees an entire subtree, closing any open descriptors that pointed into it.
`tfs_removeAll("/")` empties the disk but keeps the root itself.
