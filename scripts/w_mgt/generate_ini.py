'''
    author: Suhas Vittal
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
REPL_POLICY = 'LRU'

def write_ini(filename: str,
              write_queue_size=128,
              write_policy='ASYNC',
              wb_mode='FORCED',
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
trace_format = IMAT

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
read_queue_size = 128
write_queue_size = {write_queue_size}
write_policy = {write_policy}
sched_policy = FRFCFS
page_policy = {page_mode}
address_mapping = {address_mapping}
dram_type = 4800

[LLC]
size_kb_per_core = 2048
ways = 16
num_mshr = {32*CORES}
num_rw_ports = 4
latency = 20
read_queue_size = 64
write_queue_size = 64
prefetch_queue_size = 32
replacement_policy = {REPL_POLICY}
writeback_mode = {wb_mode}
base_cache_type = {cache_type}
dead_block_predictor = {dead_block_predictor}
''')

############################################################
############################################################
# BASELINE
write_ini('baseline')
write_ini('baseline_dead_block', dead_block_predictor='SamplingPredictor<$base>')

############################################################
############################################################
# MOTIVATION: NO WRITES
write_ini('no_writes', other_defines='DRAM_DROP_WRITES')

############################################################
############################################################
# WRITE SYNCHRONIZATION
write_ini('write_sync', write_policy='SYNC')
write_ini('write_sync_dead_block', write_policy='SYNC', dead_block_predictor='SamplingPredictor<$base>')
write_ini('write_sync_random_writes', write_policy='SYNC', other_defines='DRAM_RANDOMIZE_WRITE_ADDRESSES')
#write_ini('bank_balanced_cache', write_policy='SYNC', cache_type='BankBalancedCache')
#write_ini('bank_balanced_cache_dead_block', write_policy='SYNC', cache_type='BankBalancedCache', dead_block_predictor='SamplingPredictor<$base>')

############################################################
############################################################
