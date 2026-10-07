set pagination off
set confirm off
set architecture riscv:rv64
set disassemble-next-line on

python
import gdb

def value(expression):
    return int(gdb.parse_and_eval(expression))

def check(condition, message):
    if not condition:
        raise gdb.GdbError(message)
    print("PASS: " + message)

def stage(name):
    print("\n=== " + name + " ===")

stage("Reset ROM")
check(value("$pc") == 0x1000, "reset PC is 0x1000")
gdb.execute("x/6i $pc")
inferior = gdb.selected_inferior()
with open("bin/ucore.img", "rb") as image:
    first_bytes = image.read(16)
check(bytes(inferior.read_memory(0x80200000, 16)) == first_bytes,
      "QEMU loaded the kernel before the first CPU instruction")
for step in range(16):
    if value("$pc") == 0x80000000:
        break
    gdb.execute("si")
check(value("$pc") == 0x80000000, "reset ROM transfers control to OpenSBI")
gdb.execute("info registers pc a0 a1 a2")
gdb.execute("x/8i $pc")

stage("Kernel entry")
gdb.execute("thbreak *kern_entry")
gdb.execute("continue")
check(value("$pc") == 0x80200000, "kern_entry is reached at 0x80200000")
gdb.execute("info registers pc sp ra a0 a1 mstatus mepc satp")
gdb.execute("x/8i $pc")
stack_bottom = value("(unsigned long)&bootstack")
stack_top = value("(unsigned long)&bootstacktop")
check(stack_top - stack_bottom == 8192, "boot stack reserves two 4096-byte pages")
check(stack_top % 16 == 0, "boot stack satisfies the 16-byte ABI alignment")
original_ra = value("$ra")
for step in range(8):
    if value("$pc") == value("(unsigned long)&kern_init"):
        break
    gdb.execute("si")
check(value("$pc") == value("(unsigned long)&kern_init"), "tail reaches kern_init")
check(value("$sp") == stack_top, "la sp initializes sp to bootstacktop")
check(value("$ra") == original_ra, "tail does not overwrite ra")
gdb.execute("info registers pc sp ra")

stage("C initialization and SBI console")
gdb.execute("thbreak cprintf")
gdb.execute("continue")
start = value("(unsigned long)&edata")
end = value("(unsigned long)&end")
check(end >= start, "BSS boundaries are ordered")
check(not any(bytes(inferior.read_memory(start, end - start))) if end > start else True,
      "BSS is zero at the first cprintf (size=%d)" % (end - start))
gdb.execute("thbreak sbi_console_putchar")
gdb.execute("continue")
check(value("$a0") == ord("("), "the first console character is '('")
for step in range(40):
    if bytes(inferior.read_memory(value("$pc"), 4)) == b"\x73\x00\x00\x00":
        break
    gdb.execute("si")
check(bytes(inferior.read_memory(value("$pc"), 4)) == b"\x73\x00\x00\x00",
      "console output reaches ecall")
check(value("$a7") == 1 and value("$a0") == ord("("),
      "legacy SBI console uses a7=1 and a0=character")
gdb.execute("info registers pc a0 a1 a2 a7 stvec mtvec")
ecall_pc = value("$pc")
trap_vector = value("$mtvec") & ~3
gdb.execute("thbreak *0x%x" % trap_vector)
gdb.execute("continue")
check(value("$pc") == trap_vector, "SBI call enters the machine trap vector")
check(value("$mcause") == 9, "ecall from S mode traps to M mode with mcause=9")
check(value("$mepc") == ecall_pc, "mepc records the ecall instruction address")
check((value("$mstatus") >> 11) & 3 == 1, "MPP records the previous S mode")
gdb.execute("info registers pc mcause mepc mstatus")
gdb.execute("thbreak *0x%x" % (ecall_pc + 4))
gdb.execute("continue")
check(value("$pc") == ecall_pc + 4, "OpenSBI returns to the instruction after ecall")
gdb.execute("delete breakpoints")
print("\nLAB1 BOOT CHECKS PASSED")
end

detach
quit
