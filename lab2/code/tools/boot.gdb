set pagination off
set confirm off
set architecture riscv:rv64

python
import gdb
from pathlib import Path
MASK = (1 << 64) - 1
OFFSET = 0xffffffff40000000

def value(expression):
    return int(gdb.parse_and_eval(expression)) & MASK

def address(symbol):
    return value('(unsigned long)&' + symbol)

def check(condition, message):
    if not condition:
        raise gdb.GdbError(message)
    print('PASS: ' + message)

def stop_at(addr):
    gdb.execute('thbreak *0x%x' % addr)
    gdb.execute('continue')
    check(value('$pc') == addr, 'reached 0x%x' % addr)

def stage(name):
    print('\n=== ' + name + ' ===')

inferior = gdb.selected_inferior()
def read64(addr):
    return int.from_bytes(bytes(inferior.read_memory(addr, 8)), 'little')

stage('Reset and physical entry (Bare)')
check(value('$pc') == 0x1000 and value('$satp') == 0, 'reset PC=0x1000, satp=0')
with open(Path(gdb.current_progspace().filename).with_name('ucore.img'), 'rb') as image:
    check(bytes(inferior.read_memory(0x80200000,16)) == image.read(16), 'image loaded at PA 0x80200000')
stop_at(0x80000000)
stop_at(0x80200000)
check(value('$satp') == 0, 'OpenSBI enters the kernel in Bare mode')
hart, dtb = value('$a0'), value('$a1')
gdb.execute('info registers pc a0 a1 satp')
gdb.execute('x/14i $pc')

stage('Sv39 high entry and boot page table')
stop_at(address('kern_init'))
root_va = address('boot_page_table_sv39')
root_pa = root_va - OFFSET
satp = value('$satp')
check(satp >> 60 == 8, 'satp.MODE=8 (Sv39)')
check((satp & ((1 << 44)-1)) << 12 == root_pa, 'satp.PPN points to boot root physical address')
check(root_pa % 4096 == 0, 'root is page aligned')
check(value('$sp') == address('bootstacktop') and value('$sp') % 16 == 0, 'high stack is ABI aligned')
check(address('bootstacktop')-address('bootstack') == 8192, 'stack size is 8192 bytes')
check(value('$a0') == hart and value('$a1') == dtb, 'hart ID and DTB PA preserved across paging')
gdb.execute('info registers pc sp a0 a1 satp')
valid = [i for i in range(512) if read64(root_va+i*8) & 1]
check(valid == [2,511], 'only boot root entries 2 and 511 are valid')

def walk(va):
    table_pa = root_pa
    for level in (2,1,0):
        index = (va >> (12+9*level)) & 511
        pte = read64(table_pa+OFFSET+index*8)
        print('WALK VA=0x%016x level=%d index=%d PTE=0x%x' % (va,level,index,pte))
        if not pte & 1:
            return None
        if pte & 0xe:
            check(not (pte & 4 and not pte & 2), 'leaf W implies R')
            bits = 12+9*level
            base = (pte >> 10) << 12
            check(base & ((1 << bits)-1) == 0, 'superpage physical alignment')
            return base | (va & ((1 << bits)-1)), level, pte & 0x3ff
        table_pa = (pte >> 10) << 12
    raise gdb.GdbError('nonleaf at level zero')

for va,pa in ((0x80000000,0x80000000),(0xbfffffff,0xbfffffff),
              (0xffffffffc0000000,0x80000000),(MASK,0xbfffffff),
              (address('kern_init'),address('kern_init')-OFFSET),(dtb+OFFSET,dtb)):
    check(walk(va) == (pa,2,0xcf), 'boot VA 0x%x -> PA 0x%x, 1GiB leaf V/R/W/X/A/D' % (va,pa))
check(walk(0x40000000) is None, 'unmapped region has invalid PTE')

stage('BSS and SBI console')
stop_at(address('cprintf'))
start,end = address('edata'),address('end')
check(end > start and not any(bytes(inferior.read_memory(start,end-start))), 'BSS cleared before first print')
stop_at(address('sbi_console_putchar'))
check(value('$a0') == ord('('), 'first character is opening parenthesis')
for step in range(40):
    if bytes(inferior.read_memory(value('$pc'),4)) == b'\x73\x00\x00\x00':
        break
    gdb.execute('si')
check(bytes(inferior.read_memory(value('$pc'),4)) == b'\x73\x00\x00\x00', 'console reaches ecall')
check(value('$a7') == 1, 'legacy SBI console extension is 1')
ecall_pc = value('$pc')
stop_at(value('$mtvec') & ~3)
check(value('$mcause') == 9 and value('$mepc') == ecall_pc, 'S-mode ecall traps into M mode with cause 9')
stop_at(ecall_pc+4)

stage('DTB -> descriptors -> allocator -> selftests')
stop_at(address('run_selftests'))
check(value('va_pa_offset') == OFFSET, 'PMM uses the boot high mapping offset')
check(value('nbase') == 0x80000 and value('npage')*4096 == value('managed_end'), 'PFN base/end contract')
desc = value('(unsigned long)pages')-OFFSET
desc_end = desc+(value('npage')-value('nbase'))*value('sizeof(struct Page)')
check(desc >= address('end')-OFFSET and value('free_begin') >= desc_end, 'kernel and descriptors are reserved')
check(value('free_begin') % 4096 == 0 and value('free_begin') < value('managed_end') <= 0x88000000, 'free range aligned and capped at 128MiB')
print('PMM descriptors=[0x%x,0x%x) free=[0x%x,0x%x)' % (desc,desc_end,value('free_begin'),value('managed_end')))
gdb.execute('delete breakpoints')
print('\nLAB2 BOOT CHECKS PASSED')
end

detach
quit
