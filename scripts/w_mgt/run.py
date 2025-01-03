'''
    author: Suhas Vittal
    date:   31 December 2024
'''

from sys import argv

import os

INST_SIM = 50_000_000
INST_WARMUP = 10_000_000

which = argv[1]

if which == 'baseline':
    builds = ['BASELINE']
elif which == 'motivation':
    builds = ['NO_WRITES']
    for p in [9,11,13,15]:
        builds.append(f'WRITE_QUEUE_{p}')
else:
    print('Unknown experiment!')
    exit(1)

for b in builds:
    os.system(f'python scripts/run.py mtf/spec2017 {b} {INST_SIM} {INST_WARMUP}')
