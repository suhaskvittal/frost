'''
    author: Suhas Vittal
    date:   31 December 2024
'''

from sys import argv
import os
import time

INST_SIM = 50_000_000
#INST_WARMUP = 10_000_000
INST_WARMUP = 0

builds = []

def append_all_builds(base: str, page_modes=list[str], address_mappings=list[str]):
    for p in page_modes:
        for am in address_mappings:
            builds.append(f'{base}_{p}_{am}')

which = argv[1]

if which == 'baseline':
    append_all_builds('BASELINE', ['OP','CP'], ['COFFEELAKE', 'MOP4', 'SKYLAKE', 'ZEN'])
elif which == 'motivation':
    append_all_builds('NO_WRITES', ['CP'], ['ZEN'])
    for p in [9,11,13,15]:
        append_all_builds(f'WRITE_QUEUE_{p}', ['CP'], ['ZEN'])
elif which == 'prior_work':
    append_all_builds('EAGER', ['OP'], ['COFFEELAKE', 'SKYLAKE', 'MOP4', 'ZEN'])
    append_all_builds('VWQ', ['OP'], ['COFFEELAKE', 'SKYLAKE', 'MOP4', 'ZEN'])
else:
    print('Unknown experiment!')
    exit(1)

for b in builds:
    os.system(f'python scripts/run.py mtf/spec2017 {b} {INST_SIM} {INST_WARMUP}')
