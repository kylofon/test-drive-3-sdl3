/* Platform: DOS file wrappers (0c1c:084e-08b9), DOS memory (INT 21h 48h/49h) and the MS C 5.1 runtime
 * functions whose behaviour matters (fopen/fread/fwrite/fclose, _fmalloc/_ffree, getcwd, exit) —
 * platform.md §2.3, §2.6, §5.
 *
 * DOS memory: an MCB chain inside mem[] from HEAP_BOTTOM to HEAP_TOP, as DOS keeps it (PORT: the arena
 * starts at HEAP_BOTTOM instead of after the program's PSP block). Each block is preceded by a one-paragraph
 * MCB: byte 0 'M' (more follow) or 'Z' (last), word 1 owner (0 = free), word 3 size in paragraphs.
 *
 * Files: DOS handles are host-side FILE pointers (PORT: an OS resource, not game state), handles 5..19 as
 * DOS hands them out after the five standard ones. Names are DGROUP strings; a drive prefix "X:" and any
 * directory part are ignored and the file is looked up case-insensitively in the game directory. */
#include "platform/platform.h"
#include "platform/plat_priv.h"
#include "host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- DOS memory */

#define MCB_OWNER_FREE 0
#define MCB_OWNER_PROG 8        /* PORT: any nonzero owner; DOS stores the PSP segment */

static u8  mcb_type(u16 m)  { return rd8(m, 0); }
static u16 mcb_owner(u16 m) { return rd16(m, 1); }
static u16 mcb_size(u16 m)  { return rd16(m, 3); }
static void mcb_set(u16 m, u8 type, u16 owner, u16 size)
{
    wr8(m, 0, type);
    wr16(m, 1, owner);
    wr16(m, 3, size);
}

void plat_heap_init(void)
{
    memset(mp(HEAP_BOTTOM, 0), 0, 16);
    mcb_set(HEAP_BOTTOM, 'Z', MCB_OWNER_FREE, (u16)(HEAP_TOP - HEAP_BOTTOM - 1));
}

/* DOS 48h — platform.h */
u16 dos21_alloc(u16 paragraphs, u16 *err)
{
    u16 m = HEAP_BOTTOM;
    for (;;) {
        u8 t = mcb_type(m);
        if (t != 'M' && t != 'Z') { if (err) *err = 7; return 0; }     /* arena trashed */
        if (mcb_owner(m) == MCB_OWNER_FREE) {
            /* join the following free blocks first, as DOS does while it searches */
            while (t == 'M') {
                u16 n = (u16)(m + 1 + mcb_size(m));
                u8 nt = mcb_type(n);
                if ((nt != 'M' && nt != 'Z') || mcb_owner(n) != MCB_OWNER_FREE) break;
                mcb_set(m, nt, MCB_OWNER_FREE, (u16)(mcb_size(m) + 1 + mcb_size(n)));
                t = nt;
            }
            u16 size = mcb_size(m);
            if (size >= paragraphs) {                                   /* first fit */
                if (size > paragraphs) {
                    u16 n = (u16)(m + 1 + paragraphs);
                    mcb_set(n, t, MCB_OWNER_FREE, (u16)(size - paragraphs - 1));
                    t = 'M';
                }
                mcb_set(m, t, MCB_OWNER_PROG, paragraphs);
                if (err) *err = 0;
                return (u16)(m + 1);
            }
        }
        if (t == 'Z') break;
        m = (u16)(m + 1 + mcb_size(m));
        if (m >= HEAP_TOP) break;
    }
    if (err) *err = 8;
    return 0;
}

/* DOS 49h — platform.h */
u16 dos21_free(u16 seg)
{
    u16 m = (u16)(seg - 1);
    if (seg <= HEAP_BOTTOM || seg >= HEAP_TOP) return 9;
    u8 t = mcb_type(m);
    if (t != 'M' && t != 'Z') return 9;
    mcb_set(m, t, MCB_OWNER_FREE, mcb_size(m));
    return 0;
}

/* ---------------------------------------------------------------- DOS files */

#define DOS_MAX_HANDLES 20
#define DOS_FIRST_HANDLE 5
static FILE *dos_files[DOS_MAX_HANDLES];

static FILE *dos_file(s16 fh)
{
    if (fh < DOS_FIRST_HANDLE || fh >= DOS_MAX_HANDLES) return NULL;
    return dos_files[fh];
}

/* PORT: the file name part of a DGROUP path ("A:NAME.EXT", "C:\X\NAME.EXT" -> "NAME.EXT"). */
static const char *dos_base_name(u16 name_ds)
{
    const char *s = ds_str(name_ds);
    if (s[0] && s[1] == ':') s += 2;
    const char *b = strrchr(s, '\\');
    if (b) s = b + 1;
    b = strrchr(s, '/');
    if (b) s = b + 1;
    return s;
}

/* Opens a game file; mode as for fopen. Returns the DOS handle or -1. */
static s16 dos_open(u16 name_ds, const char *mode, bool create)
{
    s16 fh;
    for (fh = DOS_FIRST_HANDLE; fh < DOS_MAX_HANDLES && dos_files[fh]; fh++) {}
    if (fh >= DOS_MAX_HANDLES) return -1;                        /* error 4: too many open files */
    char *path = host_game_path(dos_base_name(name_ds), create);
    if (!path) return -1;                                        /* error 2: file not found */
    FILE *f = fopen(path, mode);
    host_free(path);
    if (!f) return -1;
    dos_files[fh] = f;
    return fh;
}

/* DOS 42h — platform.h */
u32 dos21_lseek(s16 fh, s32 offset, u8 whence)
{
    FILE *f = dos_file(fh);
    if (!f || whence > 2) return 0xFFFFFFFFu;
    int w = whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END;
    if (fseek(f, offset, w) != 0) return 0xFFFFFFFFu;
    long pos = ftell(f);
    return pos < 0 ? 0xFFFFFFFFu : (u32)pos;
}

/* DOS 3Fh / 40h on mem[] (the buffer is used linearly from seg:off, bounded by mem[]). */
static u16 dos_rw(FILE *f, FarPtr buf, u16 n, bool write)
{
    u32 at = far_lin(buf);
    if (at >= MEM_SIZE) return 0;
    if (n > MEM_SIZE - at) n = (u16)(MEM_SIZE - at);
    size_t k = write ? fwrite(mem + at, 1, n, f) : fread(mem + at, 1, n, f);
    if (write) fflush(f);
    return (u16)k;
}

/* 0c1c:084e dos_seek — platform.md §2.3 (INT 21h 4200h; 1 ok, 0 error) */
s16 dos_seek(s16 fh, u16 lo, u16 hi)
{
    return dos21_lseek(fh, (s32)((u32)hi << 16 | lo), 0) == 0xFFFFFFFFu ? 0 : 1;
}

/* 0c1c:0868 dos_open_read — platform.md §2.3 (INT 21h 3D00h; handle or -1) */
s16 dos_open_read(u16 name_ds)
{
    return dos_open(name_ds, "rb", false);
}

/* 0c1c:087a dos_file_size — platform.md §2.3 (low word of the size, position 0 afterwards) */
u16 dos_file_size(s16 fh)
{
    u32 size = dos21_lseek(fh, 0, 2);
    dos21_lseek(fh, 0, 0);
    return (u16)size;
}

/* 0c1c:08a0 dos_read — platform.md §2.3 (INT 21h 3Fh; AX = bytes read, or the DOS error code) */
u16 dos_read(FarPtr buf, u16 n, s16 fh)
{
    FILE *f = dos_file(fh);
    if (!f) return 6;                       /* CF set, AX = 6 (invalid handle); the caller does not check */
    return dos_rw(f, buf, n, false);
}

/* 0c1c:08b9 dos_close — platform.md §2.3 (INT 21h 3Eh) */
void dos_close(s16 fh)
{
    FILE *f = dos_file(fh);
    if (!f) return;
    fclose(f);
    dos_files[fh] = NULL;
}

/* ---------------------------------------------------------------- MS C 5.1 runtime */

/* The stream table _iob (DS:C490, 8-byte FILE records up to the last one at [DS:C5A8]):
 * +0 _ptr, +2 _cnt, +4 _base, +6 _flag (01 read, 02 write, 80h read/write), +7 _file (DOS handle). */
#define IOB_FIRST   0xC490
#define IOB_LASTPTR 0xC5A8
#define IOB_READ    0x01
#define IOB_WRITE   0x02
#define IOB_RW      0x80

/* 1940:0f42 _getstream — first free _iob record, cleared; 0 if none. */
static u16 crt_getstream(void)
{
    u16 last = DSW(IOB_LASTPTR);
    for (u16 si = IOB_FIRST;; si = (u16)(si + 8)) {
        if ((DSB(si + 6) & 0x83) == 0) {
            DSW(si + 2) = 0;
            DSB(si + 6) = 0;
            DSW(si + 4) = 0;
            DSW(si) = 0;
            DSB(si + 7) = 0xFF;
            return si;
        }
        if (si == last || si > last) return 0;
    }
}

/* 1940:0308 fopen — platform.md §2.6 */
u16 crt_fopen(u16 name_ds, u16 mode_ds)
{
    const char *m = ds_str(mode_ds);
    bool plus = strchr(m, '+') != NULL;
    const char *cm;
    u8 flag;
    bool create = false;
    switch (m[0]) {
    case 'r': cm = plus ? "r+b" : "rb"; flag = plus ? IOB_RW : IOB_READ; break;
    case 'w': cm = plus ? "w+b" : "wb"; flag = plus ? IOB_RW : IOB_WRITE; create = true; break;
    case 'a': cm = plus ? "a+b" : "ab"; flag = plus ? IOB_RW : IOB_WRITE; create = true; break;
    default: return 0;
    }
    /* PORT: text mode ('t' / default) CR-LF translation is not modelled; TD3 opens its files with "b". */
    u16 s = crt_getstream();
    if (!s) return 0;
    s16 fh = dos_open(name_ds, cm, create);
    if (fh < 0) return 0;
    DSB(s + 6) = flag;
    DSB(s + 7) = (u8)fh;
    return s;
}

static FILE *crt_file(u16 f)
{
    if (f < IOB_FIRST || f > DSW(IOB_LASTPTR) || (f - IOB_FIRST) % 8 != 0) return NULL;
    if ((DSB(f + 6) & 0x83) == 0) return NULL;
    return dos_file((s16)(s8)DSB(f + 7));
}

/* fread / fwrite on mem[]: whole items only. */
static u16 crt_rw(FarPtr buf, u16 size, u16 count, u16 f, bool write)
{
    FILE *fp = crt_file(f);
    if (!fp || size == 0 || count == 0) return 0;
    u32 total = (u32)size * count;
    u32 at = far_lin(buf), done = 0;
    while (done < total) {                  /* in pieces below 64 KB, the buffer used linearly */
        u32 k = total - done;
        if (k > 0x8000) k = 0x8000;
        if (at + done >= MEM_SIZE) break;
        if (k > MEM_SIZE - (at + done)) k = MEM_SIZE - (at + done);
        size_t r = write ? fwrite(mem + at + done, 1, k, fp) : fread(mem + at + done, 1, k, fp);
        done += (u32)r;
        if (r < k) break;
    }
    if (write) fflush(fp);
    return (u16)(done / size);
}

/* 1940:0334 fread — platform.md §2.6 */
u16 crt_fread(FarPtr buf, u16 size, u16 count, u16 f)
{
    return crt_rw(buf, size, count, f, false);
}

/* 1940:0526 fwrite — platform.md §2.6 */
u16 crt_fwrite(FarPtr buf, u16 size, u16 count, u16 f)
{
    return crt_rw(buf, size, count, f, true);
}

/* 1940:0240 fclose — platform.md §2.6 */
s16 crt_fclose(u16 f)
{
    FILE *fp = crt_file(f);
    if (!fp) return -1;
    dos_close((s16)(s8)DSB(f + 7));
    DSB(f + 6) = 0;
    DSB(f + 7) = 0xFF;
    return 0;
}

/* 1940:0681 _fmalloc — platform.md §2.6
 * PORT: each far heap block is its own DOS block (seg:0000, (size + 15) / 16 paragraphs), instead of the
 * runtime's far heap segments with block headers; callers only use the returned far pointer (render_init
 * rounds the 7810h buffer up to a paragraph itself). Sizes >= FFF1h fail as in the original. */
FarPtr crt_fmalloc(u16 size)
{
    FarPtr p = { 0, 0 };
    if (size >= 0xFFF1) return p;
    u16 err;
    u16 seg = dos21_alloc((u16)(((u32)size + 15) >> 4), &err);
    if (seg) p.seg = seg;
    return p;
}

/* 1940:066c _ffree — platform.md §2.6 */
void crt_ffree(FarPtr p)
{
    if (far_is_null(p)) return;
    dos21_free(p.seg);
}

/* 1940:07dc getcwd — platform.md §2.6 (PORT: always "C:\"; the drive letter goes only into paths whose
 * drive prefix the port ignores) */
u16 crt_getcwd(u16 buf_ds, u16 size)
{
    static const char cwd[] = "C:\\";
    if (size < sizeof cwd) return 0;
    memcpy(ds_str(buf_ds), cwd, sizeof cwd);
    return buf_ds;
}

/* 1940:01a2 exit — platform.md §2.6 */
_Noreturn void crt_exit(s16 code)
{
    for (int fh = DOS_FIRST_HANDLE; fh < DOS_MAX_HANDLES; fh++)
        if (dos_files[fh]) { fclose(dos_files[fh]); dos_files[fh] = NULL; }    /* flushall + DOS closes */
    host_shutdown();
    exit(code);
}
