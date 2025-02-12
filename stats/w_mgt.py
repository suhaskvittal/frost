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

SUITES = ['mtf/spec2017', 'mtf/gap', 'mtf/ligra']

def create_csv_file_for_build(build: str):
    wr = open(f'data/{build.lower()}.csv', 'w')

    stat_list = ['ipc', 'mpki', 'write_issue_diff', 'write_mode_time', 'miss_penalty', 'aat']
    header = ','.join(stat_list)
    wr.write(f',{header}\n')

    for suite in SUITES:
        wr.write('\n')
        workloads = [get_name(suite, f) for f in os.listdir(f'TRACES/{suite}') if f.endswith('.gz')]

        for w in workloads:
            config, results = read_output_file(f'out/{suite}/{build}/{w}.out')

            ipc =               get_per_core_stat(results, lambda d: float(d['IPC']))
            mpki =              get_per_core_stat(results, lambda d: float(d['LLC']['MPKI']))
            write_issue_diff =  float(results['DRAM']['MEAN_WRITE_ISSUE_MINMAX_DIFFERENCE']['all'])
            write_mode_time =   float(results['DRAM']['FRACTION_OF_TIME_IN_WRITE_MODE']['all'])
            miss_penalty =      get_per_core_stat(results, lambda d: float(d['LLC']['MISS_PENALTY']), mean_type=amean)
            aat =               get_per_core_stat(results, lambda d: float(d['LLC']['AAT']), mean_type=amean)

            wr.write(f'{w},{ipc},{mpki},{write_issue_diff},{write_mode_time},{miss_penalty},{aat}\n')

####################################################################
####################################################################
# RESULTS
for b in os.listdir('builds'):
    create_csv_file_for_build(b)

####################################################################
####################################################################
