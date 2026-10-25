/* lab2: 2414067 - SELF-WRITTEN tests, not the absent course basic_check/best_fit_check. */
#include <pmm.h>
#include <slub.h>
#include <selftest.h>
#include <dtb.h>
#include <stdio.h>
#include <string.h>

#define MAX_PAGES ((PHYSTOP-DRAM_BASE)/PGSIZE)
static struct Page *held[MAX_PAGES];
static unsigned char occupied[MAX_PAGES];
static uint32_t seed=0x2414067;
static uint32_t random32(void) {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void passed(const char *name) {cprintf("[PASS] %s\n",name);}
static void layout_check(void) {
    for (size_t i=0;i<npage-nbase;++i) {
        uintptr_t pa=(nbase+i)*PGSIZE;
        assert(page2pa(pages+i)==pa && pa2page(pa)==pages+i);
        assert(PADDR(KADDR(pa))==pa);
        assert(!!PageReserved(pages+i)==(pa<free_begin));
        assert(!PageAllocated(pages+i));
    }
    assert(nr_free_pages()==(managed_end-free_begin)/PGSIZE);
    assert(!alloc_pages(nr_free_pages()+1));
    passed("layout, reservations, address roundtrips, oversized allocation");
}
static void fit_check(void) {
    size_t total=nr_free_pages(); assert(total>32);
    struct Page *reserve=alloc_pages(total-32);
    struct Page *a=alloc_pages(8),*g=alloc_page(),*b=alloc_pages(4);
    struct Page *h=alloc_page(),*tail=alloc_pages(18);
    assert(reserve && a && g && b && h && tail && nr_free_pages()==0);
    free_pages(a,8); free_pages(b,4);
    assert(nr_free_pages()==12 && !alloc_pages(9));
    struct Page *selected=alloc_pages(3);
    assert(selected==(pmm_manager==&best_fit_pmm_manager ? b : a));
    free_pages(selected,3); free_page(g); free_page(h); free_pages(tail,18);
    struct Page *whole=alloc_pages(32);
    assert(whole==a); free_pages(whole,32); free_pages(reserve,total-32);
    assert(nr_free_pages()==total);
    passed("fit policy distinction, split, fragmentation failure, exact fit");

    struct Page *six=alloc_pages(6); reserve=alloc_pages(total-6);
    assert(six && reserve && nr_free_pages()==0);
    free_page(six+1); free_page(six+3); free_page(six+2);
    assert(PageProperty(six+1) && six[1].property==3);
    free_page(six); assert(six->property==4); /* right neighbor */
    free_page(six+4); assert(six->property==5); /* left neighbor */
    free_page(six+5); assert(six->property==6);
    struct Page *exact=alloc_pages(6);assert(exact==six && nr_free_pages()==0);
    free_pages(exact,6); free_pages(reserve,total-6);
    assert(nr_free_pages()==total);
    passed("left/right/both-neighbor coalescing and partial-page releases");
}
static void buddy_check(void) {
    size_t total=nr_free_pages(),count=0;
    for (unsigned order=0;order<=15;++order) {
        size_t power=(size_t)1<<order;
        for (int delta=-1;delta<=1;++delta) {
            if (power==1 && delta==-1) continue;
            size_t request=power+delta,span=1;
            while (span<request) span<<=1;
            uintptr_t first=ROUNDUP(free_begin,span*PGSIZE);
            bool possible=span<=((size_t)1<<15) && first<=managed_end &&
                          span<=(managed_end-first)/PGSIZE;
            struct Page *probe=alloc_pages(request);
            assert(!!probe==possible);
            if (probe) {
                assert(allocation_pages(probe)==span && !(page2pa(probe)%(span*PGSIZE)));
                assert(nr_free_pages()==total-span);
                free_pages(probe,request);
            }
            assert(nr_free_pages()==total);
        }
    }
    passed("buddy every order: power-of-two +/-1 boundaries, alignment and actual accounting");
    struct Page *block=alloc_pages(8);assert(block && !(page2pa(block)&(8*PGSIZE-1)));
    struct Page *p;
    while ((p=alloc_page())) {assert(count<MAX_PAGES);held[count++]=p;}
    assert(nr_free_pages()==0 && count+8==total);
    free_pages(block,8);
    struct Page *parts[8];
    for (unsigned i=0;i<8;++i) {
        parts[i]=alloc_page();assert(parts[i]);
        assert(parts[i]>=block && parts[i]<block+8);
    }
    /* Index by physical position, not by allocator list insertion order. */
    free_page(block); free_page(block+2);
    assert(nr_free_pages()==2 && !alloc_pages(2));
    assert(PageProperty(block) && block->property==0 && PageProperty(block+2));
    free_page(block+1);
    assert(block->property==1);
    struct Page *pair=alloc_pages(2);assert(pair==block);
    free_pages(pair,2); free_page(block+3);
    assert(PageProperty(block) && block->property==2);
    struct Page *four=alloc_pages(4); assert(four==block);
    free_pages(four,4);
    for (unsigned i=4;i<8;++i) free_page(block+i);
    assert(PageProperty(block) && block->property==3 && nr_free_pages()==8);
    struct Page *five=alloc_pages(5);
    assert(five==block && allocation_pages(five)==8 && nr_free_pages()==0);
    free_pages(five,5);
    for (size_t i=0;i<count;++i) free_page(held[i]);
    assert(nr_free_pages()==total);
    passed("buddy alignment, exhaustion, non-buddies, recursive merge, 5->8 rounding");
}
struct allocation {struct Page *page;size_t requested,span;unsigned char tag;};
static void release_checked(struct allocation *a) {
    size_t start=(page2pa(a->page)-free_begin)/PGSIZE;
    for (size_t i=0;i<a->span;++i) {
        assert(occupied[start+i]);
        unsigned char *data=page2kva(a->page+i);
        for (size_t j=0;j<32;++j) assert(data[j]==(unsigned char)(a->tag+i));
        occupied[start+i]=0;
    }
    free_pages(a->page,a->requested); a->page=NULL;
}
static void random_check(void) {
    struct allocation active[64]; memset(active,0,sizeof(active));
    memset(occupied,0,sizeof(occupied)); size_t total=nr_free_pages();
    seed=0x2414067;
    for (unsigned step=0;step<1200;++step) {
        unsigned slot=random32()%64;
        if (active[slot].page) release_checked(&active[slot]);
        else {
            size_t n=random32()%17+1;
            struct Page *p=alloc_pages(n); assert(p);
            size_t span=allocation_pages(p),start=(page2pa(p)-free_begin)/PGSIZE;
            assert(start+span<=MAX_PAGES);
            unsigned char tag=random32();
            for (size_t i=0;i<span;++i) {
                assert(!occupied[start+i]);occupied[start+i]=1;
                memset(page2kva(p+i),(unsigned char)(tag+i),32);
            }
            active[slot]=(struct allocation){p,n,span,tag};
        }
        size_t used=0;
        for (unsigned i=0;i<64;++i) if (active[i].page) used+=active[i].span;
        assert(nr_free_pages()+used==total);
    }
    for (unsigned i=0;i<64;++i) if (active[i].page) release_checked(&active[i]);
    for (size_t i=0;i<MAX_PAGES;++i) assert(!occupied[i]);
    assert(nr_free_pages()==total);
    passed("seed=0x2414067: 1200 random operations, independent bitmap and data isolation");
}
void pmm_check(void) {
    layout_check();
    if (pmm_manager==&buddy_pmm_manager) buddy_check(); else fit_check();
    random_check();
}
void slub_check(void) {
    static const size_t sizes[]={1,15,16,17,31,32,33,63,64,65,127,128,129,255,
        256,257,511,512,513,1023,1024,1025,2047,2048,2049,4096,4097,8193};
    void *objects[sizeof(sizes)/sizeof(sizes[0])];
    size_t total=nr_free_pages();
    assert(kmalloc(0)==NULL && kmalloc(UINT64_MAX)==NULL); kfree(NULL);
    for (size_t i=0;i<sizeof(objects)/sizeof(objects[0]);++i) {
        objects[i]=kmalloc(sizes[i]);assert(objects[i] && !((uintptr_t)objects[i]&15));
        memset(objects[i],(unsigned char)(i+1),sizes[i]);
    }
    for (size_t i=0;i<sizeof(objects)/sizeof(objects[0]);++i) {
        unsigned char *p=objects[i];
        for (size_t j=0;j<sizes[i];++j) assert(p[j]==i+1);
        kfree(p);
    }
    assert(nr_free_pages()==total);
    passed("SLUB size boundaries, 16-byte alignment, large objects and data isolation");
    void *small[600];
    for (unsigned i=0;i<600;++i) {
        small[i]=kmalloc(16);assert(small[i]);memset(small[i],(unsigned char)i,16);
    }
    assert(total-nr_free_pages()>=3);
    for (unsigned i=0;i<600;i+=2) {
        for (unsigned j=0;j<16;++j) assert(((unsigned char *)small[i])[j]==(unsigned char)i);
        kfree(small[i]);
    }
    for (unsigned i=0;i<600;i+=2) {
        small[i]=kmalloc(16);assert(small[i]);
        memset(small[i],(unsigned char)(i^0xa5),16);
    }
    for (unsigned i=0;i<600;++i) {
        unsigned char tag=(i&1) ? (unsigned char)i : (unsigned char)(i^0xa5);
        for (unsigned j=0;j<16;++j) assert(((unsigned char *)small[i])[j]==tag);
        kfree(small[i]);
    }
    assert(nr_free_pages()==total);
    size_t count=0;struct Page *p;
    while ((p=alloc_page())) {assert(count<MAX_PAGES);held[count++]=p;}
    assert(!kmalloc(32) && !kmalloc(4097));
    for (size_t i=0;i<count;++i) free_page(held[i]);
    assert(nr_free_pages()==total);
    passed("SLUB multiple slabs, interleaved free/reuse, empty-page return and OOM");
}
static void put32(unsigned char *p,uint32_t x) {
    p[0]=x>>24;p[1]=x>>16;p[2]=x>>8;p[3]=x;
}
static void dtb_check(void) {
    /* Minimal FDT: root(2 addr cells, default 1 size cell), memory@80000000. */
    unsigned char b[160];memset(b,0,sizeof(b));
    put32(b,0xd00dfeed);put32(b+4,160);put32(b+8,56);put32(b+12,144);
    put32(b+16,40);put32(b+20,17);put32(b+24,16);put32(b+32,4);put32(b+36,88);
    size_t p=56;
    put32(b+p,1);p+=8;
    put32(b+p,1);p+=4;memcpy(b+p,"memory@80000000",16);p+=16;
    put32(b+p,3);put32(b+p+4,12);put32(b+p+8,0);p+=12;
    put32(b+p,0);put32(b+p+4,0x80000000);put32(b+p+8,0x08000000);p+=12;
    put32(b+p,2);p+=4;put32(b+p,2);p+=4;put32(b+p,9);
    memcpy(b+144,"reg",4);
    struct memory_info info;
    assert(dtb_parse(b,sizeof(b),&info)==0 && info.base==DRAM_BASE && info.size==0x08000000);
    b[0]=0; assert(dtb_parse(b,sizeof(b),&info)!=0);b[0]=0xd0;
    assert(dtb_parse(b,39,&info)!=0);
    put32(b+12,159); assert(dtb_parse(b,sizeof(b),&info)!=0);put32(b+12,144);
    put32(b+88,0xffffffff);assert(dtb_parse(b,sizeof(b),&info)!=0);put32(b+88,12);
    put32(b+96,0xffffffff);put32(b+100,0xffffffff);
    assert(dtb_parse(b,sizeof(b),&info)!=0);
    passed("FDT valid fixture, magic, truncation, bounds, property length, overflow");
}
void run_selftests(void) {
    cprintf("SELFTEST: self-written B tests; not course grading\n");
    dtb_check();
#if TEST_CASE == 1
    cprintf("[EXPECT PANIC] zero-allocation\n"); alloc_pages(0);
#elif TEST_CASE == 2
    cprintf("[EXPECT PANIC] reserved-release\n"); free_page(pa2page(DRAM_BASE));
#elif TEST_CASE == 3
    struct Page *p=alloc_pages(4);assert(p);free_pages(p,4);
    cprintf("[EXPECT PANIC] double-page-free\n"); free_pages(p,4);
#elif TEST_CASE == 4
    struct Page *p=alloc_pages(5);assert(p);
    cprintf("[EXPECT PANIC] buddy-wrong-size\n"); free_pages(p,8);
#elif TEST_CASE == 5
    char *p=kmalloc(32);assert(p);
    cprintf("[EXPECT PANIC] object-interior\n"); kfree(p+1);
#elif TEST_CASE == 6
    void *p=kmalloc(32),*q=kmalloc(32);assert(p && q);kfree(p);
    cprintf("[EXPECT PANIC] double-object-free\n"); kfree(p);
#elif TEST_CASE == 7
    unsigned char bad[40]={0};struct memory_info info;
    cprintf("[EXPECT PANIC] invalid-dtb\n"); assert(dtb_parse(bad,sizeof(bad),&info)==0);
#elif TEST_CASE == 8
    cprintf("[EXPECT PANIC] descriptor-outside\n"); free_page(pages+(npage-nbase));
#elif TEST_CASE == 9
    struct Page *p=alloc_pages(5);assert(p);
    cprintf("[EXPECT PANIC] buddy-interior\n"); free_page(p+1);
#elif TEST_CASE == 10
    cprintf("[EXPECT PANIC] object-outside\n"); kfree((void *)0x1000);
#else
    pmm_manager->check();slub_check();
    cprintf("LAB2 B SELFTESTS PASSED: %s\n",pmm_manager->name);
    return;
#endif
    panic("negative test unexpectedly returned");
}
