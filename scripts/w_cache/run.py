''' 
author: Suhas Vittal
'''

############################################################
############################################################

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

############################################################
############################################################

setups = []

def add_setup(build: str, options=None, output_folder=None):
    if options is None:
        options = ''
    if output_folder is None:
        output_folder = build
    setups.append((build, options, output_folder))

############################################################
############################################################

from sys import argv
import os

CORE_ARRAY = [8]

exec_what = argv[1]

if exec_what == 'all':
    for w in ['ddr', 'baseline', 'wcache', 'vwq']:
        os.system(f'python scripts/w_cache/run.py {w}')
    exit(0)

if exec_what == 'ddr':
    for ddr_type in ['DDR3', 'DDR5']:
        for dram_page_policy in ['OPEN', 'CLOSE', 'HYBRID']:
            add_setup(f'{ddr_type}_CORE8_LRU_{dram_page_policy}')
#           add_setup(f'{ddr_type}_CORE8_LRU_{dram_page_policy}_NOTURN')

elif exec_what == 'baseline':
    for llc_repl in ['LRU', 'SRRIP']:
        for n in CORE_ARRAY:
            add_setup(f'BASELINE_CORE{n}_{llc_repl}')

elif exec_what == 'wcache':
    for llc_repl in ['LRU', 'SRRIP']:
        for n in CORE_ARRAY:
            add_setup(f'WCACHE_CORE{n}_{llc_repl}')
            add_setup(f'WCACHE_CORE{n}_{llc_repl}', f'-wcache_repl_only', f'WCACHE_CORE{n}_{llc_repl}_REPL_ONLY')

elif exec_what == 'wcache_sens':
    for fixed_pos in [2, 4, 8, 12, 16]:
        add_setup(f'WCACHE_CORE8_LRU', f'-wcache_fixed_lookup_pos {fixed_pos}', f'WCACHE_SENS_LOOKUP_{fixed_pos}')
    for sampled_sets in [8, 16, 32, 64, 128]:
        add_setup(f'WCACHE_CORE8_LRU', f'-wcache_sampled_sets {sampled_sets}', f'WCACHE_SENS_SAMPLING_{sampled_sets}')

elif exec_what == 'vwq':
    for n in CORE_ARRAY:
        add_setup(f'VWQ_CORE{n}_LRU')

############################################################
############################################################

# Print commands to run evals (ratemode part)

SUITES = ['imat/spec', 'imat/ligra', 'imat/parsec']

INST_SIM = 250_000_000

for suite in SUITES:
    inst_warmup = 250_000_000 if suite == 'mtf/gap' else 25_000_000

    traces = [f for f in os.listdir(f'TRACES/{suite}') if f.endswith('.gz')]
    for (build, args, output_folder) in setups:
        if 'DDR' in build and suite == 'mtf/gap':
            continue

        os.system(f'mkdir -p out/{suite}/{output_folder}')
        for trace_file in traces:
            trace_name = get_name(suite, trace_file)
            cmd = f'source ~/.bashrc && ./builds/{build}/sim TRACES/{suite}/{trace_file} -s {INST_SIM} -w {inst_warmup} {args} > out/{suite}/{output_folder}/{trace_name}.out &'
            print(cmd)

############################################################
############################################################

