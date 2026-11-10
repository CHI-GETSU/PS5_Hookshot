/* PS5: the game's romfs read in place from its .nup package (host/ps5/nup_romfs.h).

   The package's layout is nx2/nup.py's (that file's docstring): a TOC at 0x80 with titles, directories, files, a child
   list and the names. The newest title (the update over the base) is the game's. A file's data is at its offset, as is,
   or, when its codec byte is 1, as one zstd frame whose content is the file (40,588 of OoT's 186,587 romfs files,
   2.85 GB unpacked). Plain files are read positionally from the package; a packed one is unpacked into memory when it
   is opened and kept while any handle has it open, and the most recent ones (up to 128 MiB) stay after that.

   Paths are matched exactly, as the console's own romfs does (case-sensitive). */
#define _GNU_SOURCE
#include "nup_romfs.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zstd.h>

#define ROOT_LEN (sizeof(NUP_ROMFS_ROOT) - 1)
#define KEEP_BYTES (128ull << 20)   /* (the console's memory is shared by everything: 512 MiB was too much) */

typedef struct { uint32_t name_off, name_len, first, count; } Dir;
typedef struct { uint32_t name_off, name_len; uint64_t size, offset, stored; uint8_t codec; } File;

static int fd = -1;
static uint32_t ndirs;
static uint64_t nfiles;
static Dir *dirs;
static File *files;
static uint32_t *kids;
static size_t nkids;
static char *strings;
static uint32_t romfs_dir;
static int ready;

/* path -> node (a directory below ndirs, else ndirs + file) under the romfs, open-addressed */
static struct Slot { uint64_t hash; uint32_t node; } *table;
static size_t table_cap;

static uint64_t fnv(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 1099511628211ull;
    return h ? h : 1;
}

static int read_at(void *buf, size_t n, uint64_t at) {
    size_t done = 0;
    while (done < n) {
        const ssize_t r = pread(fd, (char *)buf + done, n - done, (off_t)(at + done));
        if (r <= 0) return 0;
        done += (size_t)r;
    }
    return 1;
}

/* the full relative path of every node is hashed while walking; the walk keeps a path buffer */
static char path_buf[4096];
static void insert(const char *path, size_t n, uint32_t node) {
    const uint64_t h = fnv(path, n);
    size_t i = h & (table_cap - 1);
    while (table[i].hash) i = (i + 1) & (table_cap - 1);
    table[i].hash = h, table[i].node = node;
}
static int walk(uint32_t d, size_t len, int depth) {
    if (depth > 64) return 0;
    const Dir *dir = &dirs[d];
    if ((uint64_t)dir->first + dir->count > nkids) return 0;
    for (uint32_t k = 0; k < dir->count; k++) {
        const uint32_t child = kids[dir->first + k];
        const uint32_t name_off = child < ndirs ? dirs[child].name_off : files[child - ndirs].name_off;
        const uint32_t name_len = child < ndirs ? dirs[child].name_len : files[child - ndirs].name_len;
        if (child >= ndirs + nfiles || len + 1 + name_len >= sizeof path_buf) return 0;
        size_t n = len;
        if (n) path_buf[n++] = '/';
        memcpy(path_buf + n, strings + name_off, name_len);
        n += name_len;
        insert(path_buf, n, child);
        if (child < ndirs && !walk(child, n, depth + 1)) return 0;
    }
    return 1;
}

int nup_romfs_open(const char *package) {
    if (ready) return 1;
    fd = open(package, O_RDONLY);
    if (fd < 0) return 0;
    uint8_t head[0x80 + 0x30];
    if (!read_at(head, sizeof head, 0) || memcmp(head, "NUP\x01", 4) || memcmp(head + 0x80, "TOC\x01", 4)) goto bad;
    uint32_t nt;
    uint64_t nd, nf, soff, ssz;
    memcpy(&nt, head + 0x84, 4), memcpy(&nd, head + 0x88, 8), memcpy(&nf, head + 0x90, 8);
    memcpy(&soff, head + 0x98, 8), memcpy(&ssz, head + 0xa0, 8);
    if (!nt || nt > 64 || nd > (1u << 24) || nf > (1u << 24) || ssz > (256u << 20)) goto bad;
    const uint64_t dirs_at = 0x80 + 0x30 + 0x40ull * nt, files_at = dirs_at + 24 * nd, kids_at = files_at + 80 * nf,
                   strings_at = 0x80 + soff;
    if (strings_at < kids_at) goto bad;
    uint8_t *meta = malloc(strings_at + ssz);
    if (!meta || !read_at(meta, strings_at + ssz, 0)) { free(meta); goto bad; }
    /* the newest title */
    uint32_t best = 0, best_ver = 0;
    for (uint32_t i = 0; i < nt; i++) {
        uint32_t ver;
        memcpy(&ver, meta + 0xb0 + 0x40 * i + 20, 4);
        if (i == 0 || ver > best_ver) best = i, best_ver = ver;
    }
    memcpy(&romfs_dir, meta + 0xb0 + 0x40 * best + 16, 4);
    ndirs = (uint32_t)nd, nfiles = nf;
    dirs = calloc(nd, sizeof *dirs), files = calloc(nf, sizeof *files);
    nkids = (size_t)((strings_at - kids_at) / 4);
    kids = malloc(nkids * 4 + 4);
    strings = malloc(ssz + 1);
    if (!dirs || !files || !kids || !strings || romfs_dir >= nd) { free(meta); goto bad; }
    for (uint64_t i = 0; i < nd; i++) memcpy(&dirs[i], meta + dirs_at + 24 * i, 16);
    for (uint64_t i = 0; i < nf; i++) {
        const uint8_t *r = meta + files_at + 80 * i;
        memcpy(&files[i].name_off, r, 4), memcpy(&files[i].name_len, r + 4, 4);
        memcpy(&files[i].size, r + 8, 8), memcpy(&files[i].offset, r + 16, 8), memcpy(&files[i].stored, r + 24, 8);
        files[i].codec = r[64];
        if ((uint64_t)files[i].name_off + files[i].name_len > ssz) { free(meta); goto bad; }
    }
    for (uint64_t i = 0; i < nd; i++)
        if ((uint64_t)dirs[i].name_off + dirs[i].name_len > ssz) { free(meta); goto bad; }
    memcpy(kids, meta + kids_at, nkids * 4);
    memcpy(strings, meta + strings_at, ssz);
    free(meta);
    for (table_cap = 1024; table_cap < (nd + nf) * 2; table_cap *= 2) {}
    table = calloc(table_cap, sizeof *table);
    if (!table || !walk(romfs_dir, 0, 0)) goto bad;
    ready = 1;
    fprintf(stderr, "[ps5] romfs: %s, title %u (v%u): %llu folders, %llu files\n", package, best, best_ver,
            (unsigned long long)nd, (unsigned long long)nf);
    return 1;
bad:
    fprintf(stderr, "[ps5] romfs: %s is not a usable .nup package\n", package);
    if (fd >= 0) close(fd);
    fd = -1;
    return 0;
}

int nup_romfs_path(const char *path) {
    return ready && !strncmp(path, NUP_ROMFS_ROOT, ROOT_LEN) && (path[ROOT_LEN] == 0 || path[ROOT_LEN] == '/');
}

int64_t nup_romfs_find(const char *path, int *is_dir, uint64_t *size) {
    if (!nup_romfs_path(path)) return -1;
    const char *rel = path + ROOT_LEN;
    while (*rel == '/') rel++;
    size_t n = strlen(rel);
    while (n && rel[n - 1] == '/') n--;
    uint32_t node;
    if (!n) {
        node = romfs_dir;
    } else {
        const uint64_t h = fnv(rel, n);
        size_t i = h & (table_cap - 1);
        for (;; i = (i + 1) & (table_cap - 1)) {
            if (!table[i].hash) return -1;
            if (table[i].hash != h) continue;
            /* (a hash match is checked against the name: the last component must be the node's) */
            const uint32_t c = table[i].node;
            const uint32_t no = c < ndirs ? dirs[c].name_off : files[c - ndirs].name_off;
            const uint32_t nl = c < ndirs ? dirs[c].name_len : files[c - ndirs].name_len;
            if (nl <= n && !memcmp(rel + n - nl, strings + no, nl) && (nl == n || rel[n - nl - 1] == '/')) { node = c; break; }
        }
    }
    if (is_dir) *is_dir = node < ndirs;
    if (size) *size = node < ndirs ? 0 : files[node - ndirs].size;
    return node;
}

/* ---- open files: a plain one reads from the package; a packed one from its unpacked bytes ---- */
typedef struct Unpacked { struct Unpacked *next; uint64_t file; uint8_t *data; int users; uint64_t last_use; } Unpacked;
static Unpacked *unpacked;
static uint64_t unpacked_bytes, use_clock;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct { uint64_t file; Unpacked *u; } Handle;
static Handle *handles;
static size_t nhandles;

static void trim(void) {   /* lock held: the least recently used unpacked files nobody holds, past KEEP_BYTES */
    while (unpacked_bytes > KEEP_BYTES) {
        Unpacked **victim = NULL;
        for (Unpacked **p = &unpacked; *p; p = &(*p)->next)
            if (!(*p)->users && (!victim || (*p)->last_use < (*victim)->last_use)) victim = p;
        if (!victim) return;
        Unpacked *u = *victim;
        *victim = u->next;
        unpacked_bytes -= files[u->file].size;
        free(u->data), free(u);
    }
}

static Unpacked *unpack(uint64_t f) {   /* lock held */
    for (Unpacked *u = unpacked; u; u = u->next)
        if (u->file == f) { u->users++, u->last_use = ++use_clock; return u; }
    const File *r = &files[f];
    uint8_t *packed = malloc(r->stored ? r->stored : 1), *data = malloc(r->size ? r->size : 1);
    if (!packed || !data || !read_at(packed, r->stored, r->offset)) { free(packed), free(data); return NULL; }
    const size_t got = ZSTD_decompress(data, r->size, packed, r->stored);
    free(packed);
    if (ZSTD_isError(got) || got != r->size) {
        fprintf(stderr, "[ps5] romfs: unpacking file %llu failed (%s)\n", (unsigned long long)f,
                ZSTD_isError(got) ? ZSTD_getErrorName(got) : "short");
        free(data);
        return NULL;
    }
    Unpacked *u = calloc(1, sizeof *u);
    if (!u) { free(data); return NULL; }
    u->file = f, u->data = data, u->users = 1, u->last_use = ++use_clock;
    u->next = unpacked, unpacked = u;
    unpacked_bytes += r->size;
    trim();
    return u;
}

int64_t nup_romfs_open_file(const char *path) {
    int is_dir = 0;
    const int64_t node = nup_romfs_find(path, &is_dir, NULL);
    if (node < 0 || is_dir) return -1;
    const uint64_t f = (uint64_t)node - ndirs;
    pthread_mutex_lock(&lock);
    Unpacked *u = NULL;
    if (files[f].codec == 1 && !(u = unpack(f))) { pthread_mutex_unlock(&lock); return -1; }
    size_t i = 0;
    while (i < nhandles && handles[i].file != UINT64_MAX) i++;
    if (i == nhandles) {
        Handle *grown = realloc(handles, (nhandles * 2 + 64) * sizeof *handles);
        if (!grown) { if (u) u->users--; pthread_mutex_unlock(&lock); return -1; }
        handles = grown;
        for (size_t k = nhandles; k < nhandles * 2 + 64; k++) handles[k].file = UINT64_MAX;
        nhandles = nhandles * 2 + 64;
    }
    handles[i].file = f, handles[i].u = u;
    pthread_mutex_unlock(&lock);
    return (int64_t)i;
}

int64_t nup_romfs_pread(int64_t h, void *buf, size_t n, uint64_t at) {
    pthread_mutex_lock(&lock);
    if (h < 0 || (size_t)h >= nhandles || handles[h].file == UINT64_MAX) { pthread_mutex_unlock(&lock); return -1; }
    const Handle hd = handles[h];
    pthread_mutex_unlock(&lock);
    const File *r = &files[hd.file];
    if (at >= r->size) return 0;
    if (n > r->size - at) n = (size_t)(r->size - at);
    if (hd.u) {   /* (held by this handle: not freed while it is open) */
        memcpy(buf, hd.u->data + at, n);
        return (int64_t)n;
    }
    return read_at(buf, n, r->offset + at) ? (int64_t)n : -1;
}

uint64_t nup_romfs_size(int64_t h) {
    pthread_mutex_lock(&lock);
    const uint64_t s = h >= 0 && (size_t)h < nhandles && handles[h].file != UINT64_MAX ? files[handles[h].file].size : 0;
    pthread_mutex_unlock(&lock);
    return s;
}

void nup_romfs_close(int64_t h) {
    pthread_mutex_lock(&lock);
    if (h >= 0 && (size_t)h < nhandles && handles[h].file != UINT64_MAX) {
        if (handles[h].u) handles[h].u->users--;
        handles[h].file = UINT64_MAX, handles[h].u = NULL;
        trim();
    }
    pthread_mutex_unlock(&lock);
}

int nup_romfs_dir_next(int64_t dir_node, uint32_t *cursor, const char **name, size_t *name_len, int *is_dir,
                       uint64_t *size) {
    if (dir_node < 0 || (uint64_t)dir_node >= ndirs) return 0;
    const Dir *d = &dirs[dir_node];
    if (*cursor >= d->count) return 0;
    const uint32_t c = kids[d->first + (*cursor)++];
    if (c < ndirs) {
        *name = strings + dirs[c].name_off, *name_len = dirs[c].name_len, *is_dir = 1, *size = 0;
    } else {
        const File *f = &files[c - ndirs];
        *name = strings + f->name_off, *name_len = f->name_len, *is_dir = 0, *size = f->size;
    }
    return 1;
}
