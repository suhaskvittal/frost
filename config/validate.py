'''
    author: Suhas Vittal
    date:   4 December 2024
'''

####################################################################
####################################################################

def update_cfg_with_optionals(cfg, optionals: list[tuple[str,str]]):
    for (key, value) in optionals:
        if key not in cfg:
            cfg[key] = value

####################################################################
####################################################################

def validate_core_section(cfg) -> bool:
    optionals = [
        ('num_threads', 1),
        ('fetch_width', 4),
        ('rob_size', 256)
    ]
    update_cfg_with_optionals(cfg, optionals)
    return True

####################################################################
####################################################################

def validate_cache_section(cfg) -> bool:
    required = [
        'ways',
        'num_mshr',
        'num_rw_ports',
        'read_queue_size',
        'write_queue_size',
        'prefetch_queue_size'
    ]
    if not all(x in cfg for x in required):
        return False
    # Now check if the number of sets or the size of the cache
    # is specified.
    if 'size_kb' in cfg:
        cfg['sets'] = str((int(cfg['size_kb']) * 1024) // (64*int(cfg['ways'])))
    elif 'sets' not in cfg:
        return False  # Have no way of determining number of cache sets
    # Now check optionals
    optionals = [
        ('mode', ''),
        ('replacement_policy', 'LRU')
    ]
    update_cfg_with_optionals(cfg, optionals)
    return True

####################################################################
####################################################################

def validate_dram_section(cfg) -> bool:
    required = [
        'channels',
        'ranks',
        'bankgroups',
        'banks',
        'rows',
        'columns',
        'read_queue_size',
        'write_queue_size'
    ]
    if not all(x in cfg for x in required):
        return False
    optionals = [
        ('BL', '16'),
        ('cmd_queue_size', '16'),
        ('page_policy', 'OPEN'),
        ('address_mapping', 'MOP4')
    ]
    update_cfg_with_optionals(cfg, optionals)
    return True

####################################################################
####################################################################
