'''
    author: Suhas Vittal date:   4 January 2025
'''

from reader import read_output_file
from accumulate import get_per_core_stat, amean, gmean, hmean

import os

####################################################################
####################################################################

from sys import argv

####################################################################
####################################################################

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

####################################################################
####################################################################

SUITES = ['imat/spec', 'imat/ligra', 'imat/parsec']
IGNORE = []

def create_csv_file_for_build(build: str, suites=None):
    if suites is None:
        suites = SUITES

    wr = open(f'data/{build.lower()}.csv', 'w')

    stat_list = ['ipc', 'llc_mpki', 'miss_penalty', 'aat', 'num_reads', 'num_writes',
                 'read_rbhr', 'write_rbhr', 'dram_cycles', 'write_cycles']
    header = ','.join(stat_list)
    wr.write(f',{header}\n')

    for suite in suites:
        wr.write('\n')
        workloads = [get_name(suite, f) for f in os.listdir(f'TRACES/{suite}') if f.endswith('.gz')]

        for w in workloads:
            if w in IGNORE:
                continue
            config, results = read_output_file(f'out/{suite}/{build}/{w}.out')

            ipc =               get_per_core_stat(results, lambda d: float(d['IPC']))
            mpki =              get_per_core_stat(results, lambda d: float(d['LLC']['MPKI']))

            num_reads =         int(results['DRAM']['NUM_READS']['all'])
            num_writes =        int(results['DRAM']['NUM_WRITES']['all'])
            read_rbhr =         float(results['DRAM']['READ_ROW_BUFFER_HIT_RATE']['all'])
            write_rbhr =        float(results['DRAM']['WRITE_ROW_BUFFER_HIT_RATE']['all'])

            dram_cycles =       int(results['DRAM']['CYCLES']['all'])
            write_cycles =      int(results['DRAM']['WRITE_MODE_CYCLES']['all'])

            miss_penalty =      get_per_core_stat(results, lambda d: float(d['LLC']['MISS_PENALTY']), mean_type=amean)
            aat =               get_per_core_stat(results, lambda d: float(d['LLC']['AAT']), mean_type=amean)

            wr.write(f'{w},{ipc},{mpki},{miss_penalty},{aat},{num_reads},{num_writes},'
                    f'{read_rbhr},{write_rbhr},{dram_cycles},{write_cycles}\n')

####################################################################
####################################################################

BUILDS = os.listdir('out/imat/spec')

for b in BUILDS:
    if 'DDR' in b:
        suites = ['imat/spec']
        continue
    else:
        suites = None

    create_csv_file_for_build(b, suites=suites)

####################################################################
####################################################################
