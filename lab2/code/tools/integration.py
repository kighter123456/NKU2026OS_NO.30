#!/usr/bin/env python3
"""C regression: PMM scenarios plus a 3-backend x 3-memory guest-GDB boot matrix."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parent.parent

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',default='qemu-system-riscv64')
    parser.add_argument('--gdb',default='gdb-multiarch')
    parser.add_argument('--boot-only',action='store_true',help='retain and validate the existing full PMM result')
    args=parser.parse_args()
    directory=ROOT/'evidence/integration'
    directory.mkdir(parents=True,exist_ok=True)
    if not args.boot_only:
        subprocess.run(['python3','tools/run_tests.py','--all','--qemu',args.qemu],cwd=ROOT,check=True)
    pmm=json.loads((ROOT/'evidence/selftest/summary-all.json').read_text())
    if not pmm['complete'] or len(pmm['results'])!=35:
        raise RuntimeError('35 PMM scenarios must pass first')
    results=[]
    def save_summary():
        (directory/'summary.json').write_text(json.dumps({'self_written':True,'complete':len(results)==9,
              'pmm_scenarios':35,'boot_gdb_results':results},indent=2)+'\n')
    save_summary()
    for backend in ('first_fit','best_fit','buddy'):
        subprocess.run(['make','-j2','PMM='+backend,'TEST_CASE=0'],cwd=ROOT,check=True)
        for memory in ('64M','128M','256M'):
            name='boot-'+backend+'-'+memory
            with (directory/(name+'.log')).open('w') as output:
                subprocess.run(['python3','tools/verify.py','--qemu',args.qemu,'--gdb',args.gdb,
                                '--image',f'bin/{backend}/0/ucore.img','--kernel',f'bin/{backend}/0/kernel',
                                '--memory',memory,'--evidence','evidence/integration/'+name],
                               cwd=ROOT,stdout=output,stderr=subprocess.STDOUT,check=True,timeout=100)
            results.append({'backend':backend,'memory':memory,'result':'passed','evidence':name})
            save_summary()
            print('PASS: '+name,flush=True)
    subprocess.run(['python3','tools/check_submission.py'],cwd=ROOT,check=True)

if __name__=='__main__':
    main()
