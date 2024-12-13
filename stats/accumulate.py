'''
    author: Suhas Vittal
    date:   13 December 2024
'''

import math

from collections import defaultdict

####################################################################
####################################################################

def gmean(arr: list[float]):
    s = sum(math.log(x) for x in arr)
    return math.exp(s/len(arr))

def hmean(arr: list[float]):
    s = sum(1.0/x for x in arr)
    return len(arr)/s

####################################################################
####################################################################

def print_header(builds: list[str]):
    BAR = ''.join('-' for _ in range(32+12*len(builds)))
    print(BAR)
    print(f'{"":<32}', ''.join(f'{b:<12}' for b in builds))
    print(BAR)

def print_footer(builds: list[str]):
    BAR = ''.join('-' for _ in range(32+12*len(builds)))
    print(f'\n{BAR}')

def get_per_core_stat(build_data, func):
    per_core = []
    i = 0
    while True:
        if f'CORE_{i}' in build_data:
            per_core.append( float(func(build_data[f'CORE_{i}'])) )
            i += 1
        else:
            break
    return hmean(per_core)

####################################################################
####################################################################

def ipc(build_data: dict, b: str, k: str, refvalue=1.0):
    return get_per_core_stat(build_data[b][k]['results'], lambda d: d['IPC'])/refvalue

####################################################################
####################################################################

def mpki(build_data: dict, b: str, k: str):
    return get_per_core_stat(build_data[b][k]['results'], lambda d: d['LLC']['MPKI'])

####################################################################
####################################################################

def rbhr(build_data: dict, b: str, k: str):
    return float(build_data[b][k]['results']['DRAM']['ROW_BUFFER_HIT_RATE']['all'])

####################################################################
####################################################################

# function to get data, should the stat be normalized?, mean-type
SUPPORTED_STATS = {
    'ipc': (ipc, True, 'gmean'),
    'mpki': (mpki, False, 'hmean'),
    'rbhr': (rbhr, False, 'hmean')
}

def print_stats(build_data: dict, builds: list[str], stat: str, ref: str):
    if stat not in SUPPORTED_STATS:
        print(f'unsupported stat: {stat}')
        return
    func, relative, mean_type = SUPPORTED_STATS[stat]

    benchmarks = build_data[builds[0]].keys()
    results = defaultdict(list)

    print_header(builds)

    for k in benchmarks:
        name = k[:k.find('.')]
        print(f'{name:<32}', end='')
        x0 = func(build_data, ref, k)
        for b in builds:
            if relative:
                x = func(build_data, b, k, x0)
            else:
                x = func(build_data, b, k)
            results[b].append(x)
            print(f'{x:<12.3f}', end='')  
        print()

    print(f'{mean_type.upper():<32}', end='')
    for b in builds:
        if mean_type == 'gmean':
            x = gmean(results[b])
        else:
            x = hmean(results[b])
        print(f'{x:<12.3f}', end='')

    print_footer(builds)

####################################################################
####################################################################
