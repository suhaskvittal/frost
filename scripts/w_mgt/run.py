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

def append_all_defaults(base: str):
    for (p, am) in [('OP', 'MOP4'), ('CP', 'ZEN')]:
        builds.append(f'{base}_{p}_{am}')

which = argv[1]

if which == 'all':
    for w in ['baseline', 'motivation', 'handshake']:
        os.system(f'python scripts/w_mgt/run.py {w}')
        print('sleeping for 5 minutes...')
        time.sleep(300)
    exit(0)

if which == 'baseline':
    append_all_defaults('BASELINE')
elif which == 'motivation':
    append_all_defaults('NO_WRITES')
    for p in [9,11,13,15]:
        append_all_defaults(f'WRITE_QUEUE_{p}')
elif which == 'handshake':
    append_all_defaults('WRITE_HAND')
else:
    print('Unknown experiment!')
    exit(1)

for b in builds:
    os.system(f'python scripts/run.py mtf/spec2017 {b} {INST_SIM} {INST_WARMUP}')
