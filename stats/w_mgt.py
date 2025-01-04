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

    with open(output_file, 'w') as wr:
        header = ','.join(f'\"{b}\"' for b in builds)
        wr.write(f'{header}\n')
        for w in common_workloads:
            wname = w[:w.find('.out')]
            # Read results for all builds
            results_map = {
                b : read_output_file(f'out/{suite}/{b}/{w}') for b in builds
            }
            baseline = builds[0]  # This is the assumption
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

all_am = ['COFFEELAKE', 'SKYLAKE', 'MOP4', 'ZEN']

####################################################################
####################################################################
# BASELINE TESTS
baseline_op_builds = [f'BASELINE_OP_{am}' for am in all_am]
baseline_cp_builds = [f'BASELINE_CP_{am}' for am in all_am]
create_csv_file_for_ipc('baseline_test_op.ipc.csv', 'mtf/spec2017', *baseline_op_builds)
create_csv_file_for_ipc('baseline_test_cp.ipc.csv', 'mtf/spec2017', *baseline_cp_builds)

####################################################################
####################################################################
# MOTIVATION
write_queue_builds = [f'WRITE_QUEUE_{p}_CP_ZEN' for p in [9,11]]
create_csv_file_for_ipc('motivation.ipc.csv', 'mtf/spec2017',
        'BASELINE_CP_ZEN',
        'NO_WRITES_CP_ZEN',
        *write_queue_builds)

exit(1)

####################################################################
####################################################################
# PRIOR WORK
baseline_builds = [f'BASELINE_OP_{am}' for am in all_am]
eager_builds = [f'EAGER_OP_{am}' for am in all_am]
vwq_builds = [f'VWQ_OP_{am}' for am in all_am]
create_csv_file_for_ipc('prior_work.ipc.csv', 'mtf/spec2017',
        *baseline_builds, *eager_builds, *vwq_builds)

####################################################################
####################################################################
