'''
    author: Suhas Vittal
    date:   31 December 2024
'''

from sys import argv
import os
import time

############################################################
############################################################

WHERE = 'AMPERE'

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
    if WHERE == 'PACE':
        os.system(f'sbatch -N1 --ntasks-per-node=1 --account=gts-mqureshi4-rg -t8:00:00 -o {out} --wrap=\"{call}\"')
    else:
        print(f'source ~/.bashrc && {call} > {out} &')

############################################################
############################################################

INST_SIM = 100_000_000
INST_WARMUP = 0

builds = []
other_args = {}

page_mode = argv[1]
which = argv[2]

prefix = 'OP' if page_mode == 'open' else 'CP'

def append_build(base: str, add_args=''):
    builds.append((f'{prefix}_{base}', add_args))

if which == 'all':
    for w in ['baseline', 'no_writes', 'random_writes', 'evals']:
        os.system(f'python scripts/w_mgt/run.py {page_mode} {w}')
        if WHERE == 'PACE':
            print('sleeping for 15 minutes...')
            time.sleep(15*60)
    exit(0)

if which == 'baseline':
    append_build('BASELINE_LRU')
    append_build('BASELINE_SRRIP')
    append_build('BASELINE_DRRIP')
elif which == 'no_writes':
    append_build('NO_WRITES')
elif which == 'random_writes':
    append_build('RANDOM_WRITES')
elif which == 'evals':
    append_build('VWQ_LRU')
    append_build('BALANCED_CACHE_LRU')
    append_build('BALANCED_CACHE_SRRIP')
    append_build('BALANCED_CACHE_DRRIP')
elif which == 'dead_block':
    append_build('BASELINE_LRU_DB')
    append_build('BALANCED_CACHE_LRU_DB')
    append_build('BALANCED_CACHE_SRRIP_DB')
    append_build('BALANCED_CACHE_DRRIP_DB')
else:
    print('Unknown experiment!')
    exit(1)

############################################################
############################################################

for suite in ['mtf/spec2017', 'mtf/gap']:
    inst_warmup = 250_000_000 if suite == 'mtf/gap' else INST_WARMUP

    benchmarks = [f for f in os.listdir(f'TRACES/{suite}') if f.endswith('.xz') or f.endswith('.gz')]
    for (build, add_args) in builds:
        os.system(f'mkdir -p out/{suite}/{build}')
        for b in benchmarks:
            name = get_name(suite, b)
            base_cmd = f'./builds/{build}/sim TRACES/{suite}/{b} -s {INST_SIM} -w {inst_warmup} {add_args}'

            issue_sbatch(base_cmd, f'out/{suite}/{build}/{name}.out')
