/* PS5: the game's romfs read in place from its .nup package (host/ps5/nup_romfs.c), so the console needs no 26 GB
   extracted copy: the package ProsperoEden already reads (/data/prosperoeden/roms/oot/oot.nup) serves both.
   host/posix/win32_posix.c's file calls send every path under NUP_ROMFS_ROOT here. */
#ifndef HOS_NUP_ROMFS_H
#define HOS_NUP_ROMFS_H
#include <stddef.h>
#include <stdint.h>

#define NUP_ROMFS_ROOT "/nup/romfs"

/* Opens the package and indexes its newest title's romfs; 1 when it is usable. */
int nup_romfs_open(const char *package);
/* Whether a path is under NUP_ROMFS_ROOT (and the package is open). */
int nup_romfs_path(const char *path);
/* Node of a path: >= 0, and *is_dir and *size set; -1 when there is no such file or folder. */
int64_t nup_romfs_find(const char *path, int *is_dir, uint64_t *size);
/* A file's bytes [at, at + n): the count read, -1 on an error. A file the package keeps as a zstd frame is unpacked
   once into memory (kept while open, the last few cached). */
int64_t nup_romfs_open_file(const char *path);   /* a handle, -1 when not a file */
int64_t nup_romfs_pread(int64_t handle, void *buf, size_t n, uint64_t at);
uint64_t nup_romfs_size(int64_t handle);
void nup_romfs_close(int64_t handle);
/* A folder's entries, one by one: 1 with the name and kind, 0 at the end. cursor starts at 0. */
int nup_romfs_dir_next(int64_t dir_node, uint32_t *cursor, const char **name, size_t *name_len, int *is_dir, uint64_t *size);
#endif
