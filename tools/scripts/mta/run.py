'''
    author: Suhas Vittal
    date:   18 February 2025
'''

import os
from sys import argv

#################################################################
#################################################################

def get_name(suite, filename):
    left, right = 0, filename.find('.')
    # The if statements here are just for special cases.
    if suite == 'ctf/spec2017':
        left, right = filename.find('.')+1, filename.find('_s')
    elif suite == 'ligra':
        return filename
    elif suite == 'parsec':
        return filename
    elif suite == 'mtf/spec2017':
        right = filename.find('_17')
    return filename[left:right]

#################################################################
#################################################################

EXEC_WHAT = argv[1]

#################################################################
#################################################################

# TRACE GENERATION

if EXEC_WHAT == 'generate':
    assoc = int(argv[2])

    if not os.path.isdir(f'out/mta/traces/assoc{assoc}'):
        os.system(f'mkdir -p out/mta/traces/assoc{assoc}')

    suites = ['ctf/spec2017']
    for s in suites:
        INST_SIM = 500_000_000
        INST_SKIP = 0

        traces = [f for f in os.listdir(f'../TRACES/{s}') if f.endswith('.gz') or f.endswith('.xz')]
        for tr in traces:
            name = get_name(s, tr)
            output_file = f'out/mta/traces/{name}.out'
            # Create miss trace:
            print(f'./build/cachesim ../TRACES/{s}/{tr} -s {INST_SIM} -w {INST_SKIP} -assoc {assoc} -size_kb 2048 -miss_trace out/mta/traces/assoc{assoc}/{name}.out')

#################################################################
#################################################################

# TRACE ANALYSIS

if EXEC_WHAT == 'miss_analysis':
    assoc = int(argv[2])

    if not os.path.isdir(f'out/mta/results/assoc{assoc}'):
        os.system(f'mkdir -p out/mta/results/assoc{assoc}')

    traces = [f for f in os.listdir(f'out/mta/traces/assoc{assoc}')]
    for tr in traces:
        print(f'./build/mta out/mta/traces/assoc{assoc}/{tr} 1000000000 > out/mta/results/assoc{assoc}/{tr}')

#################################################################
#################################################################

if EXEC_WHAT == 'headroom':
    assoc = int(argv[2])

    suites = ['ctf/spec2017']
    for s in suites:
        INST_SIM = 500_000_000
        INST_SKIP = 0

        traces = [f for f in os.listdir(f'../TRACES/{s}') if f.endswith('.gz') or f.endswith('.xz')]
        for tr in traces:
            name = get_name(s, tr)
            if not os.path.isdir(f'out/headroom/assoc{assoc}/{s}/{name}'):
                os.system(f'mkdir -p out/headroom/assoc{assoc}/{s}/{name}')

            print(f'./build/cachesim ../TRACES/{s}/{tr} -s {INST_SIM} -assoc {assoc} -size_kb 2048 > out/headroom/assoc{assoc}/{s}/{name}/baseline.out')
            print(f'./build/cachesim ../TRACES/{s}/{tr} -s {INST_SIM} -assoc {assoc} -size_kb 2048 -memento > out/headroom/assoc{assoc}/{s}/{name}/memento.out')


#################################################################
#################################################################
