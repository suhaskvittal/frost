'''
    author: Suhas Vittal
    date:   4 January 2025
'''

from reader import read_output_file
from accumulate import get_per_core_stat, gmean, hmean

import os

####################################################################
####################################################################

def create_csv_file_for_ipc(output_file: str, suite: str, *builds):
    avail_workloads = {
        b : [f for f in os.listdir(f'out/{suite}/{b}') if f.endswith('out')] for b in builds
    }
    common_workloads = [x for x in avail_workloads[builds[0]] if\
                            all((x in out_list) for (_,out_list) in avail_workloads.items())]

    ipc_list = {b: [] for b in builds}

    with open(f'data/{output_file}', 'w') as wr:
        header = ','.join(f'\"{b}\"' for b in builds)
        wr.write(f'{header}\n')
        for w in common_workloads:
            wname = w[:w.find('.out')]
            # Read results for all builds
            results_map = {
                b : read_output_file(f'out/{suite}/{b}/{w}') for b in builds
            }
            baseline = builds[0]  # This is the assumption
            mpki = get_per_core_stat(results_map[baseline][1], lambda d: d['LLC']['MPKI'])
            if mpki < 1.0:
                continue
            # Construct array for line
            data_list = [wname]
            base_ipc = get_per_core_stat(results_map[baseline][1], lambda d: d['IPC'])
            for b in builds:
                rel_ipc = get_per_core_stat(results_map[b][1], lambda d: float(d['IPC'])/base_ipc)
                data_list.append(str(f'{rel_ipc:.3f}'))
                ipc_list[b].append(rel_ipc)
            line = ','.join(data_list)
            wr.write(f'{line}\n')
        # Write gmean data
        wr.write('gmean')
        for (_, arr) in ipc_list.items():
            ipc = gmean(arr)
            wr.write(f',{ipc:.3f}')
        wr.write('\n')

####################################################################
####################################################################
# MOTIVATION
for suffix in ['OP_MOP4', 'CP_ZEN']:
    builds = [f'BASELINE_{suffix}', f'NO_WRITES_{suffix}']
    for p in [9]:
        builds.append(f'WRITE_QUEUE_{p}_{suffix}')
    s = 'op' if suffix == 'OP_MOP4' else 'cp'
    create_csv_file_for_ipc(f'motivation_{s}.ipc.csv', 'mtf/spec2017', *builds)

####################################################################
####################################################################
# WRITE HANDSHAKING
for suffix in ['OP_MOP4', 'CP_ZEN']:
    builds = [f'BASELINE_{suffix}', f'NO_WRITES_{suffix}', f'WRITE_QUEUE_9_{suffix}', f'WRITE_HAND_{suffix}']
    s = 'op' if suffix == 'OP_MOP4' else 'cp'
    create_csv_file_for_ipc(f'handshaking_{s}.ipc.csv', 'mtf/spec2017', *builds)

####################################################################
####################################################################
