''' 
author: Suhas Vittal
'''

############################################################
############################################################

from sys import argv
import os

############################################################
############################################################

OUTPUT_FOLDER = 'ini/simple_core/w_cache'

TRACE_FORMAT = 'IMAT'

DRAM_READ_QUEUE_SIZE = 64

LLC_SIZE_KB_PER_CORE = 2048
LLC_WAYS = 16

def write_ini(filename: str,
              # SYSTEM PARAMETERS
              num_cores=8,
              # DRAM PARAMETERS
              dram_write_queue_size=48,
              dram_page_policy='HYBRID',
              dram_address_mapping='ZEN',
              dram_am_enable_permutation=True,
              # LLC PARAMETERS
              llc_repl='LRU',
              llc_type='Cache',
              # OTHER:
              dram_disable_turnaround=False,
              dram_use_ddr3_setup=False
):
    defines = 'DRAM_TRACK_ADVANCED_STATS'

    if dram_disable_turnaround:
        defines += ',DRAM_DISABLE_TURNAROUND'

    if dram_am_enable_permutation:
        defines += ',DRAM_AM_ENABLE_PERMUTATION'

    BL = 16
    dram_type = '4800'
    dram_freq = 2.4
    dram_channels = 2 * ((num_cores-1)//8 + 1)
    dram_banks = 4
    dram_bankgroups = 8

    if dram_use_ddr3_setup:
        BL = 8
        dram_type = 'ddr3_1600'
        dram_freq = 0.8
        dram_banks = 1
        dram_bankgroups = 8

    output_file = f'{OUTPUT_FOLDER}/{filename}'
    base_folder = os.path.dirname(output_file)
    
    if not os.path.isdir(base_folder):
        print(base_folder)
        os.system(f'mkdir -p {base_folder}')

    with open(output_file, 'w') as wr:
        wr.write(
f'''[SYSTEM]
model = simple
defines = {defines}
trace_format = {TRACE_FORMAT}

[CORE]
frequency_ghz = 4.0
num_threads = {num_cores}
fetch_width = 4
rob_size = 384

[DRAM]
dram_type = {dram_type}
frequency_ghz = {dram_freq}
channels = {dram_channels}
ranks = 1
bankgroups = {dram_bankgroups}
banks = {dram_banks}
rows = 65536
columns = 128
BL = {BL}
read_queue_size = {DRAM_READ_QUEUE_SIZE}
write_queue_size = {dram_write_queue_size}
sched_policy = FRFCFS
page_policy = {dram_page_policy}
address_mapping = {dram_address_mapping}

[LLC]
size_kb_per_core = {LLC_SIZE_KB_PER_CORE}
ways = {LLC_WAYS}
replacement_policy = {llc_repl}
read_queue_size = 16
write_queue_size = 16
prefetch_queue_size = 8
latency = 20
num_mshr_per_core = 32
fill_queue_size = 32
read_ports = 4
write_ports = 4
fill_ports = 1
cache_type = {llc_type}
''')

############################################################
############################################################

# Page mode, DDR3, and RBHR comparison:
for dram_address_mapping in ['MOP4', 'ZEN']:
    for dram_page_policy in ['OPEN', 'CLOSE', 'HYBRID']:
        pp = dram_page_policy.lower()

        write_ini(f'ddr3_comparison/ddr5_core8_lru_{pp}.ini', num_cores=8,
                  dram_page_policy=dram_page_policy, dram_address_mapping=dram_address_mapping)
        write_ini(f'ddr3_comparison/ddr3_core8_lru_{pp}.ini', num_cores=8, 
                  dram_page_policy=dram_page_policy, dram_address_mapping=dram_address_mapping,
                  dram_use_ddr3_setup=True)

#        write_ini(f'ddr3_comparison/ddr5_core8_lru_{pp}_noturn.ini', num_cores=8,
#                  dram_page_policy=dram_page_policy, dram_address_mapping=dram_address_mapping,
#                  dram_disable_turnaround=True)
#        write_ini(f'ddr3_comparison/ddr3_core8_lru_{pp}_noturn.ini', num_cores=8,
#                  dram_page_policy=dram_page_policy, dram_address_mapping=dram_address_mapping,
#                  dram_use_ddr3_setup=True, dram_disable_turnaround=True)

############################################################
############################################################

CORE_ARRAY = [8,16]
REPL_ARRAY = ['LRU', 'SRRIP']

# Baseline (hybrid page policy)
for llc_repl in REPL_ARRAY:
    r = llc_repl.lower()
    for n in CORE_ARRAY:
        write_ini(f'main/baseline_core{n}_{r}.ini', num_cores=n, llc_repl=llc_repl)

############################################################
############################################################

# WCache:
for llc_repl in REPL_ARRAY:
    r = llc_repl.lower()
    for n in CORE_ARRAY:
        write_ini(f'main/wcache_core{n}_{r}.ini', num_cores=n, llc_repl=llc_repl, llc_type='WCache')

############################################################
############################################################

# Configs for VWQ evaluations (fixed to LRU)
for n in CORE_ARRAY:
    write_ini(f'vwq_eval/vwq_core{n}_lru.ini', num_cores=n, llc_repl='LRU', llc_type='VirtualWriteQueue')

############################################################
############################################################
