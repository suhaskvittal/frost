'''
    author: Suhas Vittal
'''

############################################################
############################################################

CORES = 8

def write_ini(filename: str,
              page_mode='CLOSE',
              write_queue_size=128,
              write_policy='ASAP',
              wb_mode='FORCED',
              address_mapping='ZEN',
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
address_mapping = {address_mapping}
dram_type = 4800

[LLC]
size_kb_per_core = 2048
ways = 16
num_mshr = 512
num_rw_ports = 4
latency = 20
read_queue_size = 64
write_queue_size = 64
prefetch_queue_size = 32
replacement_policy = LRU
writeback_mode = {wb_mode}
''')

############################################################
############################################################

def make_filename(basename: str, page_mode: str, address_mapping: str) -> str:
    page_str = 'op' if page_mode == 'OPEN' else 'cp'
    am_str = address_mapping.lower()
    return f'ini/simple_core/w_mgt/{basename}_{page_str}_{am_str}.ini'

############################################################
############################################################
# BASELINE
for page_mode in ['OPEN', 'CLOSE']:
    for am in ['ZEN', 'COFFEELAKE', 'MOP4', 'SKYLAKE']:
        write_ini(make_filename('baseline', page_mode, am), address_mapping=am, page_mode=page_mode)

############################################################
############################################################
# MOTIVATION: NO WRITES + SPEEDUP WITH WRITE BUFFER
write_ini(make_filename('no_writes', 'CLOSE', 'ZEN'), page_mode='CLOSE', address_mapping='ZEN', other_defines='DRAM_DROP_WRITES')
for p in [9, 11, 13, 15]:
    write_ini(make_filename(f'write_queue_{p}', 'CLOSE', 'ZEN'), page_mode='CLOSE', address_mapping='ZEN', write_queue_size=2**p)

############################################################
############################################################
# MOTIVATION: EAGER WRITEBACK AND VWQ, open page
for am in ['ZEN', 'COFFEELAKE', 'MOP4', 'SKYLAKE']:
    write_ini(make_filename('eager', 'OPEN', am), address_mapping=am, page_mode='OPEN', wb_mode='EAGER')
    write_ini(make_filename('vwq', 'OPEN', am), address_mapping=am, page_mode='OPEN', wb_mode='VIRTUAL_WRITE_QUEUE')

