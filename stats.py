''' author: Suhas Vittal
    date:   12 December 2024

    This is interactive.
'''

from stats.reader import *
from stats.accumulate import *

import os

####################################################################
####################################################################

CURR_BUILDS = ['BASELINE', 'NO_WRITES']
CURR_REFERENCE = 'BASELINE'
CURR_SUITE = None

BUILD_DATA = defaultdict(dict)

def load_suite(suite):
    for b in CURR_BUILDS: 
        for f in os.listdir(f'out/{suite}/{b}'):
            if not f.endswith('.out'):
                continue
            name = f[:f.find('.')]
            config, results = read_output_file(f'out/{suite}/{b}/{f}')
            BUILD_DATA[b][f] = {'config': config, 'results': results}

while True:
    cmd = input('Enter command:\t')

    cmd_parts = cmd.split()
    cmd = cmd_parts[0]
    args = cmd_parts[1:]

    if cmd == 'exit' or cmd == 'x':
        exit(0)
    elif cmd == 'clear-screen':
        os.system('clear')
    elif cmd == 'show':
        spol = '   '.join(CURR_BUILDS)
        print(f'''---------- STATUS -------------
CURRENT BUILDS    = {spol}
CURRENT REFERENCE = {CURR_REFERENCE}
CURRENT B-SUITE   = {CURR_SUITE}
-------------------------------
''')
    elif cmd == 'add-build':
        build = args[0]
        CURR_BUILDS.append(build)
        if CURR_SUITE is not None:
            load_suite(CURR_SUITE)
    elif cmd == 'load-suite':
        suite = args[0]
        load_suite(suite)
        CURR_SUITE = suite
    elif cmd == 'rm-build':
        build = args[0]
        CURR_BUILDS.remove(build)
        del BUILD_DATA[build]
    elif cmd == 'set-ref-build':
        build = args[0]
        CURR_REFERENCE = build
    elif cmd == 'print':
        stat_name = args[0]
        print_stats(BUILD_DATA, CURR_BUILDS, stat_name, CURR_REFERENCE)

####################################################################
####################################################################
