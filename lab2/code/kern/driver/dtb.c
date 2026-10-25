/* lab2: 2414067 - bounded FDT v17 parser for QEMU's contiguous DRAM bank. */
#include <dtb.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <memlayout.h>

static struct memory_info memory;
static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static bool region(size_t offset, size_t length, size_t total) {
    return offset <= total && length <= total-offset;
}
static bool terminated(const unsigned char *p, size_t n) {
    for (size_t i=0; i<n; ++i) if (!p[i]) return 1;
    return 0;
}
static uint64_t cells(const unsigned char *p, unsigned n) {
    return n==1 ? be32(p) : ((uint64_t)be32(p)<<32)|be32(p+4);
}
struct node {
    unsigned ac, sc, parent_ac, parent_sc;
    bool is_memory;
    const unsigned char *reg;
    size_t reglen;
};
int dtb_parse(const void *blob, size_t available, struct memory_info *out) {
    const unsigned char *b=blob;
    if (!b || !out || available<40 || be32(b)!=0xd00dfeed) return -1;
    size_t total=be32(b+4), so=be32(b+8), to=be32(b+12);
    size_t ro=be32(b+16), ts=be32(b+32), ss=be32(b+36);
    if (total<40 || total>available || be32(b+20)<17 || be32(b+24)>17 ||
        so<40 || to<40 || ro<40 || (so&3) || (ro&7) ||
        !region(so,ss,total) || !region(to,ts,total) ||
        !(so+ss<=to || to+ts<=so)) return -1;
    /* Validate the reservation-map terminator, without retaining pointers. */
    size_t r=ro;
    for (;;) {
        if (!region(r,16,total) || !(r+16<=so || r>=so+ss) ||
            !(r+16<=to || r>=to+ts)) return -1;
        if (!cells(b+r,2) && !cells(b+r+8,2)) break;
        r+=16;
    }
    struct node stack[16];
    unsigned depth=0;
    bool root_seen=0, root_done=0, found=0;
    struct memory_info result={0,0};
    size_t pos=so, end=so+ss;
    while (region(pos,4,end)) {
        uint32_t token=be32(b+pos); pos+=4;
        if (token==1) { /* FDT_BEGIN_NODE */
            if (depth==16 || root_done || !terminated(b+pos,end-pos)) return -1;
            const char *name=(const char *)(b+pos);
            if (!depth && (root_seen || *name)) return -1;
            struct node *n=&stack[depth];
            n->ac=2; n->sc=1; n->reg=NULL; n->reglen=0;
            n->parent_ac=depth ? stack[depth-1].ac : 2;
            n->parent_sc=depth ? stack[depth-1].sc : 1;
            n->is_memory=!strcmp(name,"memory") || !strncmp(name,"memory@",7);
            pos=ROUNDUP(pos+strlen(name)+1,4);
            if (pos>end) return -1;
            ++depth; root_seen=1;
        } else if (token==2) { /* FDT_END_NODE */
            if (!depth) return -1;
            struct node *n=&stack[--depth];
            if (n->is_memory) {
                unsigned ac=n->parent_ac, sc=n->parent_sc;
                if (!n->reg || ac<1 || ac>2 || sc<1 || sc>2 ||
                    !n->reglen || n->reglen%((ac+sc)*4)) return -1;
                for (size_t i=0;i<n->reglen;i+=(ac+sc)*4) {
                    uint64_t base=cells(n->reg+i,ac), size=cells(n->reg+i+ac*4,sc);
                    if (!size || base>UINT64_MAX-size) return -1;
                    if (!found) {result.base=base;result.size=size;found=1;}
                    else if (result.base+result.size==base) result.size+=size;
                    else return -1; /* Deliberately reject discontiguous banks. */
                }
            }
            if (!depth) root_done=1;
        } else if (token==3) { /* FDT_PROP */
            if (!depth || !region(pos,8,end)) return -1;
            size_t len=be32(b+pos), nameoff=be32(b+pos+4); pos+=8;
            if (!region(pos,len,end) || nameoff>=ts ||
                !terminated(b+to+nameoff,ts-nameoff)) return -1;
            const char *name=(const char *)(b+to+nameoff);
            struct node *n=&stack[depth-1];
            if (!strcmp(name,"#address-cells") || !strcmp(name,"#size-cells")) {
                if (len!=4) return -1;
                if (!strcmp(name,"#address-cells")) n->ac=be32(b+pos);
                else n->sc=be32(b+pos);
            } else if (!strcmp(name,"reg")) {
                if (n->reg) return -1;
                n->reg=b+pos; n->reglen=len;
            } else if (!strcmp(name,"device_type") && len==7 &&
                       !memcmp(b+pos,"memory",7)) n->is_memory=1;
            pos=ROUNDUP(pos+len,4);
            if (pos>end) return -1;
        } else if (token==4) continue; /* FDT_NOP */
        else if (token==9) {
            if (depth || !root_done || !found) return -1;
            *out=result; return 0;
        } else return -1;
    }
    return -1;
}
void dtb_init(uintptr_t hartid, uintptr_t dtb_pa) {
    /* Physical accessibility is a platform precondition before reading header. */
    const uintptr_t platform_end=DRAM_BASE+0x40000000ULL;
    assert(dtb_pa>=DRAM_BASE && dtb_pa<=platform_end-40 && !(dtb_pa&7));
    size_t available=platform_end-dtb_pa;
    if (available>0x200000) available=0x200000;
    assert(dtb_parse((void *)(dtb_pa+KERNEL_VA_PA_OFFSET),available,&memory)==0);
    cprintf("DTB: hart=%lu pa=0x%016lx RAM=[0x%016lx,0x%016lx) size=%lu MiB\n",
            hartid,dtb_pa,memory.base,memory.base+memory.size,memory.size>>20);
}
uintptr_t get_memory_base(void) {return memory.base;}
size_t get_memory_size(void) {return memory.size;}
