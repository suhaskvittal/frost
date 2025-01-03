'''
    author: Suhas Vittal
'''

############################################################
############################################################

CORES = 4
ADDRESS_MAPPING = 'ZEN'

def write_ini(filename: str,
              page_mode='CLOSE',
              write_queue_size=128,
              write_policy='ASAP',
              other_defines=''):
    with open(filename, 'w') as wr:
        wr.write(
f'''[SYSTEM]
model = simple
defines = {other_defines}

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
cmd_queue_size = 16
page_policy = {page_mode}
address_mapping = {ADDRESS_MAPPING}
dram_type = 4800

[LLC]
size_kb_per_core = 2048
ways = 16
num_mshr = 64
num_rw_ports = 4
latency = 20
read_queue_size = 64
write_queue_size = 64
prefetch_queue_size = 32
replacement_policy = LRU
''')

############################################################
############################################################
# BASELINE
write_ini('ini/simple_core/w_mgt/baseline.ini')

############################################################
############################################################
# MOTIVATION: NO WRITES + SPEEDUP WITH WRITE BUFFER
write_ini('ini/simple_core/w_mgt/no_writes.ini', other_defines='DRAM_DROP_WRITES')
for p in [9, 11, 13, 15]:
    write_ini(f'ini/simple_core/w_mgt/write_queue_{p}.ini', write_queue_size=2**p)

