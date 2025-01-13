'''
    author: Suhas Vittal
'''

############################################################
############################################################

CORES = 8

def write_ini(filename: str,
              page_mode='CLOSE',
              write_queue_size=128,
              write_policy='ASYNC',
              wb_mode='FORCED',
              address_mapping='ZEN',
              cache_type='Cache',
              other_defines=''):

    if len(other_defines) > 0:
        other_defines += ','
    other_defines += 'DRAM_TRACK_ADVANCED_STATS'

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
num_mshr = {128*CORES}
num_rw_ports = 4
latency = 20
read_queue_size = 64
write_queue_size = 64
prefetch_queue_size = 32
replacement_policy = LRU
writeback_mode = {wb_mode}
base_cache_type = {cache_type}
''')

############################################################
############################################################

def make_filename(basename: str, page_mode: str, address_mapping: str) -> str:
    page_str = 'op' if page_mode == 'OPEN' else 'cp'
    am_str = address_mapping.lower()
    return f'ini/simple_core/w_mgt/{basename}_{page_str}_{am_str}.ini'

def get_default_mapping(page_mode: str) -> str:
    return 'MOP4' if page_mode == 'OPEN' else 'ZEN'

############################################################
############################################################
# BASELINE
for page_mode in ['OPEN', 'CLOSE']:
    am = get_default_mapping(page_mode)
    write_ini(make_filename('baseline', page_mode, am), address_mapping=am, page_mode=page_mode)

############################################################
############################################################
# WATERMARK
for page_mode in ['OPEN', 'CLOSE']:
    am = get_default_mapping(page_mode)
    write_ini(make_filename('watermark', page_mode, am), address_mapping=am, page_mode=page_mode, other_defines='DRAM_USE_WATERMARKS_TO_DRAIN')

############################################################
############################################################
# MOTIVATION: NO WRITES + SPEEDUP WITH WRITE BUFFER
for page_mode in ['OPEN', 'CLOSE']:
    am = get_default_mapping(page_mode)
    write_ini(make_filename('no_writes', page_mode, am), page_mode=page_mode, address_mapping=am, other_defines='DRAM_DROP_WRITES')
    for p in [9, 11]:
        write_ini(make_filename(f'write_queue_{p}', page_mode, am), page_mode=page_mode, address_mapping=am, write_queue_size=2**p)

############################################################
############################################################
# WRITE SYNCHRONIZATION
for page_mode in ['OPEN', 'CLOSE']:
    am = get_default_mapping(page_mode)
    write_ini(make_filename('write_sync', page_mode, am), page_mode=page_mode, address_mapping=am, write_policy='SYNC')
    write_ini(make_filename('write_sync_balanced_cache', page_mode, am), page_mode=page_mode, address_mapping=am, write_policy='SYNC', cache_type='BankBalancedCache')

############################################################
############################################################
