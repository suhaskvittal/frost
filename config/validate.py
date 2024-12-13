'''
    author: Suhas Vittal
    date:   4 December 2024
'''

####################################################################
####################################################################

def update_cfg_with_optionals(cfg, optionals: list[tuple[str,str]]):
    for (key, value) in optionals:
        if key not in cfg:
            cfg[key] = str(value)

####################################################################
####################################################################

def validate_system_section(cfg) -> bool:
    optionals = [
        ('model', 'complex'),
        ('defines', '')
    ]
    update_cfg_with_optionals(cfg, optionals)
    return True

####################################################################
####################################################################

def validate_core_section(cfg) -> bool:
    optionals = [
        ('num_threads', 1),
        ('fetch_width', 4),
        ('rob_size', 256),
        ('ftb_size', 64)
    ]
    update_cfg_with_optionals(cfg, optionals)
    return True

####################################################################
####################################################################

def validate_cache_section(cfg) -> bool:
    optionals = [
        ('sets', 64),
        ('ways', 8),
        ('num_mshr', 8),
        ('num_rw_ports', 2),
        ('read_queue_size', 64),
        ('write_queue_size', 64),
        ('prefetch_queue_size', 32),
        ('latency', 4),
        ('mode', ''),
        ('replacement_policy', 'LRU')
    ]
    # Now check if the number of sets or the size of the cache
    # is specified.
    if 'size_kb' in cfg:
        cfg['sets'] = str((int(cfg['size_kb']) * 1024) // (64*int(cfg['ways'])))
    # Now check optionals
    update_cfg_with_optionals(cfg, optionals)
    return True

####################################################################
####################################################################

def validate_dram_section(cfg) -> bool:
    optionals = [
        ('frequency_ghz', 2.4),
        ('channels', 2),
        ('ranks', 1),
        ('bankgroups', 8),
        ('banks', 4),
        ('rows', 65536),
        ('columns', 128),
        ('BL', '16'),
        ('cmd_queue_size', '16'),
        ('page_policy', 'OPEN'),
        ('address_mapping', 'MOP4')
    ]
    update_cfg_with_optionals(cfg, optionals)
    return True

####################################################################
####################################################################

def validate_os_section(cfg) -> bool:
    optionals = [
        ('levels', 4),
        ('ptwc_3_sw', '1:2'),
        ('ptwc_2_sw', '1:4'),
        ('ptwc_1_sw', '4:8')
    ]
    update_cfg_with_optionals(cfg, optionals)
    num_levels = int(cfg['levels'])
    return all(f'ptwc_{i}_sw' in cfg for i in range(1, num_levels))

####################################################################
####################################################################
