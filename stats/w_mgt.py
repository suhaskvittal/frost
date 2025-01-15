'''
    author: Suhas Vittal
    date:   4 January 2025
'''

from reader import read_output_file
from accumulate import get_per_core_stat, gmean, hmean

import os

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

SUITES = ['mtf/spec2017', 'mtf/gap']

def create_csv_file_for_ipc(output_file: str, *builds):
    wr = open(f'data/{output_file}', 'w')

    header = ','.join(f'{b}' for b in builds)
    wr.write(f'{header}\n')
    ipc_list = {b: [] for b in builds}
    for suite in SUITES:
        workloads = [get_name(suite, f) for f in os.listdir(f'TRACES/{suite}') if f.endswith('.mtf.gz')]
        local_ipc_list = {b: [] for b in builds}
        for w in workloads:
            # Read results for all builds
            results_map = {
                b : read_output_file(f'out/{suite}/{b}/{w}.out') for b in builds
            }
            baseline = builds[0]  # This is the assumption
            mpki = get_per_core_stat(results_map[baseline][1], lambda d: d['LLC']['MPKI'])
            if mpki < 1.0:
                continue
            # Construct array for line
            data_list = [w]
            base_ipc = get_per_core_stat(results_map[baseline][1], lambda d: d['IPC'])
            for b in builds:
                rel_ipc = get_per_core_stat(results_map[b][1], lambda d: float(d['IPC']))/base_ipc
                data_list.append(str(f'{rel_ipc:.3f}'))
                ipc_list[b].append(rel_ipc)
                local_ipc_list[b].append(rel_ipc)
            line = ','.join(data_list)
            wr.write(f'{line}\n')
        wr.write(f'gmean_{suite}')
        for (_, arr) in local_ipc_list.items():
            ipc = gmean(arr)
            wr.write(f',{ipc:.3f}')
        wr.write('\n')
    # Write gmean data
    wr.write('gmean')
    for (_, arr) in ipc_list.items():
        ipc = gmean(arr)
        wr.write(f',{ipc:.3f}')
    wr.write('\n')
    wr.close()

####################################################################
####################################################################

def create_csv_file_for_ipc_scan(output_file: str, scans: list[int], folder: str, baseline_folder: str):
    wr = open(f'data/{output_file}', 'w')
    header = ','.join(f'SCAN_{i}' for i in scans)
    wr.write(f'{baseline_folder},{header}\n')

    ipc_list = {i: [] for i in scans}
    for suite in SUITES:
        local_ipc_list = {i: [] for i in scans}
        workloads = [get_name(suite, f) for f in os.listdir(f'TRACES/{suite}') if f.endswith('.mtf.gz')]
        for w in workloads:
            # First read baseline results:
            _, baseline_results = read_output_file(f'out/{suite}/{baseline_folder}/{w}.out')
            mpki = get_per_core_stat(baseline_results, lambda d: d['LLC']['MPKI'])
            if mpki < 1.0:
                continue
            base_ipc = get_per_core_stat(baseline_results, lambda d: d['IPC'])
            # Create array for line
            results_map = {
                i : read_output_file(f'out/{suite}/{folder}/{w}_scan{i}.out') for i in scans
            }
            data_list = [w,'1.0']
            for i in scans:
                rel_ipc = get_per_core_stat(results_map[i][1], lambda d: float(d['IPC']))/base_ipc
                data_list.append(str(f'{rel_ipc:.3f}'))
                ipc_list[i].append(rel_ipc)
                local_ipc_list[i].append(rel_ipc)
            line = ','.join(data_list)
            wr.write(f'{line}\n')
        wr.write(f'gmean_{suite},1.0')
        for (_, arr) in local_ipc_list.items():
            ipc = gmean(arr)
            wr.write(f',{ipc:.3f}')
        wr.write('\n')
    # Write gmean data
    wr.write('gmean,1.0')
    for (_, arr) in ipc_list.items():
        ipc = gmean(arr)
        wr.write(f',{ipc:.3f}')
    wr.write('\n')

####################################################################
####################################################################

def get_folder(base: str, page_mode: str):
    return f'{base}_OP_MOP4' if page_mode == 'op' else f'{base}_CP_ZEN'

####################################################################
####################################################################
# MOTIVATION
for s in ['cp']:
    builds = [get_folder('BASELINE', s), get_folder('NO_WRITES', s)]
    for p in [9, 11]:
        builds.append(get_folder(f'WRITE_QUEUE_{p}', s))
    create_csv_file_for_ipc(f'motivation_{s}.ipc.csv', *builds)

####################################################################
####################################################################
# WRITE SYNCHRONIZATION SCAN
for s in ['cp']:
#   create_csv_file_for_ipc_scan(f'write_sync_{s}.ipc.csv', [1,2,4,8,16,128], get_folder('WRITE_SYNC', s), get_folder('BASELINE', s))
#   create_csv_file_for_ipc_scan(f'balanced_cache_{s}.ipc.csv', [1,2,4,8], get_folder('WRITE_SYNC_BALANCED_CACHE', s), get_folder('BASELINE', s))
    create_csv_file_for_ipc(f'main_results_1_{s}.ipc.csv',
                            get_folder('BASELINE', s), 
                            get_folder('NO_WRITES', s), 
                            get_folder('WRITE_SYNC', s),
                            get_folder('WRITE_SYNC_BALANCED_CACHE', s))

####################################################################
####################################################################
