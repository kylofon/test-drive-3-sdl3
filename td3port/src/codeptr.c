#include "codeptr.h"

#include "host.h"

#define CODEPTR_MAX 512

static struct { u32 addr; CodeFn fn; } table[CODEPTR_MAX];
static int count;

void codeptr_register(u32 fn_addr, CodeFn fn)
{
    for (int i = 0; i < count; i++) {
        if (table[i].addr == fn_addr) { table[i].fn = fn; return; }
    }
    if (count == CODEPTR_MAX) host_fatal("codeptr table full");
    table[count].addr = fn_addr;
    table[count].fn = fn;
    count++;
}

FarPtr codeptr_far(u32 fn_addr)
{
    return far_make(SEG((u16)(fn_addr >> 16)), (u16)fn_addr);
}

CodeFn codeptr_lookup(FarPtr p)
{
    if (far_is_null(p)) return NULL;
    u32 addr = (u32)(u16)(p.seg - LOAD_SEG) << 16 | p.off;
    for (int i = 0; i < count; i++) {
        if (table[i].addr == addr) return table[i].fn;
    }
    host_fatal("call through unregistered code pointer %04X:%04X", (unsigned)(p.seg - LOAD_SEG), (unsigned)p.off);
}

CodeFn codeptr_lookup_near(u16 file_seg, u16 off)
{
    u32 addr = (u32)file_seg << 16 | off;
    for (int i = 0; i < count; i++) {
        if (table[i].addr == addr) return table[i].fn;
    }
    host_fatal("call through unregistered near code pointer %04X:%04X", (unsigned)file_seg, (unsigned)off);
}
