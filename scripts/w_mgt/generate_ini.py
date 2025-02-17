''' author: Suhas Vittal
'''

############################################################
############################################################

from sys import argv
import os

page_mode = argv[1]

if page_mode == 'open':
    output_folder = 'ini/simple_core/w_mgt/op'
else:
    output_folder = 'ini/simple_core/w_mgt/cp'
if not os.path.isdir(output_folder):
    os.system(f'mkdir -p {output_folder}')

address_mapping = 'MOP4' if page_mode == 'open' else 'ZEN'
page_mode = page_mode.upper()

############################################################
############################################################

CORES = 8
TRACE_FORMAT = 'MTF'

DEFAULT_QUEUE_SIZE = 128

def write_ini(filename: str,
              queue_size=DEFAULT_QUEUE_SIZE,
              write_policy='ASYNC',
              repl='LRU',
              cache_type='Cache',
              dead_block_predictor='NoDeadBlockPredictor',
              other_defines=''
):
    if len(other_defines) > 0:
        other_defines += ','
    other_defines += 'DRAM_TRACK_ADVANCED_STATS'

    with open(f'{output_folder}/{filename}.ini', 'w') as wr:
        wr.write(
f'''[SYSTEM]
model = simple
defines = {other_defines}
trace_format = {TRACE_FORMAT}

[CORE]
frequency_ghz = 4.0
num_threads = {CORES}
fetch_width = 4
rob_size = 384

[DRAM]
frequency_ghz = 2.4
channels = 2
ranks = 1
bankgroups = 8
banks = 4
rows = 65536
columns = 128
BL = 16
read_queue_size = {queue_size}
write_queue_size = {queue_size}
sched_policy = FRFCFS
page_policy = {page_mode}
write_policy = {write_policy}
address_mapping = {address_mapping}
dram_type = 4800

[LLC]
size_kb_per_core = 2048
ways = 16
replacement_policy = {repl}
read_queue_size = 64
write_queue_size = 64
prefetch_queue_size = 32
latency = 20
num_mshr_per_core = 32
fill_queue_size = 32
read_ports = 4
write_ports = 4
fill_ports = 1
cache_type = {cache_type}
dead_block_predictor = {dead_block_predictor}
''')

############################################################
############################################################
# BASELINE
write_ini('baseline_lru', repl='LRU')
write_ini('baseline_drrip', repl='DRRIP')

############################################################
############################################################
# MOTIVATION
write_ini('no_writes', other_defines='DRAM_DROP_WRITES')
write_ini('random_writes', other_defines='DRAM_RANDOMIZE_WRITE_ADDRESSES', write_policy='ASYNC')

############################################################
############################################################
# IMPL
write_ini('vwq_lru', cache_type='VirtualWriteQueue', repl='LRU')

write_ini('balanced_cache_lru', cache_type='BalancedWritebackCache', repl='LRU')
write_ini('balanced_cache_drrip', cache_type='BalancedWritebackCache', repl='DRRIP')

############################################################
############################################################
