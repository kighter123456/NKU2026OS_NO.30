#!/usr/bin/env python3
"""Make browser-readable excerpts from real GDB logs, retaining original line numbers."""
import hashlib
import html
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
directory=ROOT/'evidence/qemu-mmu'
log=(directory/'host-gdb.log').read_text()
lines=log.splitlines()
digest=hashlib.sha256((directory/'host-gdb.log').read_bytes()).hexdigest()

def section(start,end):
    first=next(i for i,line in enumerate(lines) if start in line)
    last=next((i for i in range(first+1,len(lines)) if end in lines[i]),len(lines))
    return '\n'.join('%04d  %s' % (i+1,lines[i]) for i in range(first,last))

panels=[
    ('01-bare','Bare: software TLB miss and identity translation',
     section('PASS: Bare first lookup','=== Sv39 software')),
    ('02-sv39','Sv39: actual source stepping and 1 GiB leaf',
     section('=== Sv39 single-step','=== Sv39 subsequent')),
    ('03-tlb-store','TLB hit, guest store, and translated physical page',
     section('=== Sv39 subsequent','=== Sv39 store single-step')+'\n\n[... see full log for intermediate source steps ...]\n\n'+
     section('PASS: Sv39 store VA=','QEMU SOURCE MMU CHECKS PASSED')),
]
for name,title,text in panels:
    body='''<!doctype html><html lang="en"><meta charset="utf-8"><title>'''+title+'''</title>
<style>body{margin:0;padding:28px;background:#eef1f5;color:#172233;font:16px Arial,sans-serif}
h1{font-size:23px;margin:0 0 12px}p{line-height:1.5}pre{padding:22px;background:#101a28;color:#dce8f5;
font:14px/1.55 Consolas,monospace;white-space:pre-wrap;overflow-wrap:anywhere;border-radius:8px}
small{font-size:12px;overflow-wrap:anywhere}</style><h1>'''+title+'''</h1>
<p>Screenshot of real GDB transcript excerpts, rendered in a browser. Not a live terminal capture.<br>
Source: code/evidence/qemu-mmu/host-gdb.log. Original line numbers retained.</p>
<small>Source SHA256: '''+digest+'''</small><pre>'''+html.escape(text)+'''</pre></html>'''
    (directory/(name+'.html')).write_text(body,encoding='utf-8')
    print(directory/(name+'.html'))
