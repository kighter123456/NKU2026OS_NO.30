"""Run inside native GDB, against the unmodified QEMU 6.2.0 debug binary."""
import gdb
import os

TARGET = int(os.environ['LAB2_MMU_TARGET'], 16)
STORE_TARGET = int(os.environ['LAB2_MMU_STORE_TARGET'], 16)
MASK = (1 << 64)-1

def val(expr):
    return int(gdb.parse_and_eval(expr)) & MASK

def check(ok, text):
    if not ok:
        raise gdb.GdbError(text)
    print('PASS: ' + text, flush=True)

def bp(spec, condition):
    point = gdb.Breakpoint(spec)
    point.condition = condition
    return point

def lookup(label, target, first=False):
    print('\n=== ' + label + ' software TLB lookup ===', flush=True)
    point = bp('accel/tcg/cputlb.c:1506', 'addr == 0x%x' % target)
    gdb.execute('run' if first else 'continue')
    point.delete()
    check(val('addr') == target, 'conditional breakpoint matched guest fetch VA')
    gdb.execute('bt 5')
    gdb.execute('p/x addr')
    gdb.execute('p/x env->satp')
    gdb.execute('p mmu_idx')
    gdb.execute('p/x entry->addr_code')
    miss = val('entry->addr_code') & (MASK ^ 4095) != target & (MASK ^ 4095)
    check(miss, label + ' first lookup misses software TLB')

def translate(label, target, resume=True):
    print('\n=== ' + label + ' single-step page translation ===', flush=True)
    point = bp('get_physical_address', 'addr == 0x%x && !is_debug' % target)
    if resume:
        gdb.execute('continue')
    point.delete()
    check(val('addr') == target and val('is_debug') == 0, 'CPU access, not debugger memory read')
    physical_ptr = val('physical')
    prot_ptr = val('prot')
    gdb.execute('bt 5')
    # Each next advances a real QEMU C source statement; never replace guest results.
    snapshots = set()
    for step in range(160):
        frame = gdb.selected_frame()
        if frame.name() != 'get_physical_address':
            break
        line = frame.find_sal().line
        fields = {
            509: ('addr','vm'),
            537: ('addr','base','vm','levels','ptidxbits','ptesize','ptshift'),
            550: ('addr','base','i','idx','ptshift'),
            587: ('pte_addr','pte'),
            593: ('pte','ppn'),
            670: ('addr','ppn','ptshift'),
            675: ('*physical',),
            687: ('*physical','*prot'),
        }
        if line in fields and line not in snapshots:
            snapshots.add(line)
            print('SNAPSHOT cpu_helper.c:%d' % line, flush=True)
            for expr in fields[line]:
                try:
                    print('%s = 0x%x' % (expr,val(expr)), flush=True)
                except gdb.error:
                    pass
        gdb.execute('next')
    else:
        raise gdb.GdbError('translation did not return')
    pa = val('*(unsigned long long*)0x%x' % physical_ptr)
    prot = val('*(int*)0x%x' % prot_ptr)
    expected = target if label == 'Bare' else target-0xffffffff40000000
    check(pa == expected and prot == 7, '%s VA=0x%x -> PA=0x%x prot=R/W/X' % (label,target,pa))
    if label.startswith('Sv39'):
        check(593 in snapshots and 687 in snapshots, 'observed PTE permission checks and successful leaf return')
    # Observe the actual cache fill after PMP validation.
    point = bp('tlb_set_page', 'vaddr == 0x%x' % (target & ~4095))
    gdb.execute('continue')
    point.delete()
    gdb.execute('info args')
    check(val('paddr') == expected & ~4095, label + ' fills software TLB with translated physical page')

lookup('Bare',0x80200000,True)
translate('Bare',0x80200000)
lookup('Sv39',TARGET)
translate('Sv39',TARGET)
print('\n=== Sv39 subsequent fetch on the same page ===', flush=True)
point = bp('accel/tcg/cputlb.c:1506', '(addr & ~4095ULL) == 0x%x' % (TARGET & ~4095))
gdb.execute('continue')
point.delete()
gdb.execute('p/x addr')
gdb.execute('p/x entry->addr_code')
check(val('entry->addr_code') & ~4095 == val('addr') & ~4095, 'subsequent fetch hits software TLB')
gdb.execute('next')
print('The source next skips the miss branch; a hit needs no page walk.', flush=True)
print('\n=== First BSS store by guest memset ===', flush=True)
point = bp('get_physical_address', 'addr == 0x%x && access_type == 1 && !is_debug' % STORE_TARGET)
gdb.execute('continue')
point.delete()
gdb.execute('info args')
translate('Sv39 store',STORE_TARGET,resume=False)
print('\nQEMU SOURCE MMU CHECKS PASSED', flush=True)
gdb.execute('detach')
