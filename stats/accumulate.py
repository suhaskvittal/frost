'''
    author: Suhas Vittal
    date:   13 December 2024
'''

import math

from collections import defaultdict

####################################################################
####################################################################

MPKI_LIMIT = 1.0

####################################################################
####################################################################

def amean(arr: list[float]):
    return sum(arr) / len(arr)

def gmean(arr: list[float]):
    s = sum(math.log(x) for x in arr)
    return math.exp(s/len(arr))

def hmean(arr: list[float]):
    s = sum(1.0/x for x in arr)
    return len(arr)/s

####################################################################
####################################################################

def get_per_core_stat(build_data, func, mean_type=hmean):
    per_core = []
    i = 0
    while True:
        if f'CORE_{i}' in build_data:
            per_core.append( float(func(build_data[f'CORE_{i}'])) )
            i += 1
        else:
            break
    return mean_type(per_core)

####################################################################
####################################################################
