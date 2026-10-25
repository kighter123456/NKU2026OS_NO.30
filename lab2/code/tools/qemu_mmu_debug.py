#!/usr/bin/env python3
"""Reproduce native-GDB + guest-GDB MMU tracing on upstream QEMU 6.2.0."""
import argparse
import os
from pathlib import Path
import signal
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', required=True, help='unstripped QEMU 6.2.0 debug executable')
    args = parser.parse_args()
    os.chdir(ROOT)
    evidence = ROOT/'evidence'/'qemu-mmu'
    evidence.mkdir(parents=True,exist_ok=True)
    subprocess.run(['make','-j2','PMM=best_fit','TEST_CASE=0'],check=True)
    kernel = ROOT/'bin/best_fit/0/kernel'
    image = kernel.with_name('ucore.img')
    symbols = subprocess.check_output(['riscv64-unknown-elf-nm',str(kernel)],text=True)
    target = next(int(line.split()[0],16) for line in symbols.splitlines() if line.split()[-1]=='kern_init')
    store_target = next(int(line.split()[0],16) for line in symbols.splitlines() if line.split()[-1]=='edata')
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0))
        port = listener.getsockname()[1]
    env = dict(os.environ,LAB2_MMU_TARGET=hex(target),LAB2_MMU_STORE_TARGET=hex(store_target))
    source = 'source tools/qemu_host.py'
    command = ['gdb','-q','-nx','-batch','-ex','set pagination off','-ex','set confirm off',
               '-ex','handle SIGPIPE nostop noprint pass','-ex',source,'--args',args.qemu,
               '-machine','virt','-m','128M','-display','none','-monitor','none',
               '-serial','file:'+str(evidence/'qemu.log'),'-bios','default','-kernel',str(image),
               '-gdb','tcp:127.0.0.1:'+str(port),'-S']
    guest = None
    with (evidence/'host-gdb.log').open('w') as hostlog, (evidence/'guest-gdb.log').open('w') as guestlog:
        hostlog.write('$ '+' '.join(command)+'\n'); hostlog.flush()
        host = subprocess.Popen(command,env=env,stdout=hostlog,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            deadline=time.monotonic()+20
            while True:
                if host.poll() is not None:
                    raise RuntimeError('native GDB exited; inspect host-gdb.log')
                try:
                    with socket.create_connection(('127.0.0.1',port),timeout=.2):
                        break
                except OSError:
                    if time.monotonic()>deadline:
                        raise RuntimeError('QEMU GDB stub not ready')
                    time.sleep(.05)
            guest_commands = ['set pagination off','set confirm off','target remote 127.0.0.1:'+str(port),
                              'thbreak *0x80200000','continue','info registers pc satp',
                              'thbreak kern_init','continue','info registers pc sp satp','x/6i $pc',
                              'thbreak memset','continue','x/12i $pc','info registers a0 a1 a2',
                              'thbreak run_selftests','continue','info registers pc satp','detach']
            guest_command = ['gdb-multiarch','-q','-nx','-batch',str(kernel)]
            for item in guest_commands:
                guest_command += ['-ex',item]
            guestlog.write('$ '+' '.join(guest_command)+'\n'); guestlog.flush()
            guest = subprocess.Popen(guest_command,stdout=guestlog,stderr=subprocess.STDOUT)
            if guest.wait(timeout=120):
                raise RuntimeError('guest GDB failed')
            if host.wait(timeout=20):
                raise RuntimeError('host GDB failed')
            deadline = time.monotonic()+20
            while 'LAB2 B SELFTESTS PASSED:' not in (evidence/'qemu.log').read_text(errors='replace'):
                if time.monotonic()>deadline:
                    raise RuntimeError('kernel selftests did not finish after debugging')
                time.sleep(.1)
        finally:
            if guest and guest.poll() is None:
                guest.kill(); guest.wait()
            try:
                os.killpg(host.pid,signal.SIGTERM)
            except ProcessLookupError:
                pass
            if host.poll() is None:
                host.wait(timeout=5)
    text = (evidence/'host-gdb.log').read_text(errors='replace')
    if 'QEMU SOURCE MMU CHECKS PASSED' not in text:
        raise RuntimeError('source trace did not complete')
    print('PASS: Bare/Sv39 source stepping, PTE, TLB miss/fill/hit, dual GDB and kernel selftests')

if __name__=='__main__':
    main()
