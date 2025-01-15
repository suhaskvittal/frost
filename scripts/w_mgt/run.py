'''
    author: Suhas Vittal
    date:   31 December 2024
'''

from sys import argv
import os
import time

############################################################
############################################################

WHERE = 'PACE'

def get_name(suite, filename):
    left, right = 0, filename.find('.')
    # The if statements here are just for special cases.
    if suite == 'spec2017_4xx' or suite == 'spec2017_6xx':
        left, right = filename.find('.')+1, filename.find('_s')
    elif suite == 'ligra':
        return filename
    elif suite == 'parsec':
        return filename
    elif suite == 'mtf/spec2017':
        right = filename.find('_17')
    return filename[left:right]

ISSUE_CNT = 0

def issue_sbatch(call: str, out: str):
    global ISSUE_CNT
    ISSUE_CNT += 1
#   os.system(f'sbatch -N1 --ntasks-per-node=1 --account=gts-mqureshi4-rg -t8:00:00 -o {out} --wrap=\"{call}\"')
    print(f'{call} > {out} &')
    os.system(f'{call} > {out} &\n')

############################################################
############################################################

INST_SIM = 100_000_000
#INST_WARMUP = 10_000_000
INST_WARMUP = 0

builds = []

other_args = {}

def append_all_defaults(base: str):
    for (p, am) in [('CP','ZEN')]:
        builds.append(f'{base}_{p}_{am}')

which = argv[1]

if which == 'all':
    for w in ['baseline', 'no_writes', 'sync', 'bank-balanced-cache']:
        os.system(f'python scripts/w_mgt/run.py {w}')
        print('sleeping for 15 minutes...')
        time.sleep(15*60)
    exit(0)

if which == 'baseline':
    append_all_defaults('BASELINE')
elif which == 'no_writes':
    append_all_defaults('NO_WRITES')
elif which == 'motivation':
    for p in [9,11]:
        append_all_defaults(f'WRITE_QUEUE_{p}')
elif which == 'watermark-scan':
    append_all_defaults('WATERMARK')
elif which == 'sync' or which == 'sync-scan':
    append_all_defaults('WRITE_SYNC')
elif which == 'bank-balanced-cache' or which == 'bank-balanced-cache-scan':
    append_all_defaults('WRITE_SYNC_BALANCED_CACHE')
else:
    print('Unknown experiment!')
    exit(1)

############################################################
############################################################

for suite in ['mtf/spec2017', 'mtf/gap']:
    benchmarks = [f for f in os.listdir(f'TRACES/{suite}') if f.endswith('.xz') or f.endswith('.gz')]
    for build in builds:
        os.system(f'mkdir -p out/{suite}/{build}')
        for b in benchmarks:
            name = get_name(suite, b)
            print(f'BENCHMARK {b}')
            base_cmd = f'./builds/{build}/sim TRACES/{suite}/{b} -s {INST_SIM} -w {INST_WARMUP}'
            if which == 'watermark-scan':
                for x in [0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3]:
                    cmd = f'{base_cmd} -dram_wm_low {x} -dram_wm_high 1.0'
                    issue_sbatch(cmd, f'out/{suite}/{build}/{name}_scan{int(x*100)}.out')
            elif which == 'sync-scan':
                for ii in [1, 2, 4, 8, 16, 128]:
                    cmd = f'{base_cmd} -dram_wsync_count {ii}'
                    issue_sbatch(cmd, f'out/{suite}/{build}/{name}_scan{ii}.out')
                time.sleep(120)
            elif which == 'bank-balanced-cache-scan':
                for ii in [1, 2, 4, 8]:
                    cmd = f'{base_cmd} -dram_wsync_count {ii}'
                    issue_sbatch(cmd, f'out/{suite}/{build}/{name}_scan{ii}.out')
                time.sleep(120)
            else:
                issue_sbatch(base_cmd, f'out/{suite}/{build}/{name}.out')

print(f'issued {ISSUE_CNT} jobs')
