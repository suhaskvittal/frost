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

def create_csv_file_for_ipc(output_file: str, suite: str, *builds):
    workloads = [get_name(suite, f) for f in os.listdir(f'TRACES/{suite}') if f.endswith('.mtf.gz')]
    ipc_list = {b: [] for b in builds}

    with open(f'data/{output_file}', 'w') as wr:
        header = ','.join(f'{b}' for b in builds)
        wr.write(f'{header}\n')
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

def create_csv_file_for_ipc_scan(output_file: str, suite: str, scan_low: int, scan_high: int, scan_step: int, folder: str, baseline_folder: str):
    workloads = [get_name(suite, f) for f in os.listdir(f'TRACES/{suite}') if f.endswith('.mtf.gz')]

    scans = list(range(scan_low, scan_high+1, scan_step))

    ipc_list = {i: [] for i in scans}

    with open(f'data/{output_file}', 'w') as wr:
        header = ','.join(f'SCAN_{i}' for i in scans)
        wr.write(f'{baseline_folder},{header}\n')
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
            line = ','.join(data_list)
            wr.write(f'{line}\n')
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
for s in ['op','cp']:
    builds = [get_folder('BASELINE', s), get_folder('NO_WRITES', s)]
    for p in [9, 11]:
        builds.append(get_folder(f'WRITE_QUEUE_{p}', s))
    create_csv_file_for_ipc(f'motivation_{s}.ipc.csv', 'mtf/spec2017', *builds)

####################################################################
####################################################################
# WATERMARK SCAN
for s in ['op', 'cp']:
    create_csv_file_for_ipc_scan(
            f'watermark_{s}.ipc.csv', 'mtf/spec2017', 0, 30, 5, get_folder('WATERMARK', s), get_folder('BASELINE', s))

####################################################################
####################################################################
# WRITE SYNCHRONIZATION SCAN
for op in [True, False]:
    for avg in [True, False]:
        s1, s2 = ('op' if op else 'cp'), ('avg' if avg else 'noavg')
#       create_csv_file_for_ipc_scan(f'wsync_scan_{s1}_{s2}.ipc.csv', 'mtf/spec2017', open_page=op, use_average=avg)

####################################################################
####################################################################
