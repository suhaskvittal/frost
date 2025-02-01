'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from config.validate import *
from config import constants, memsys, globs, sim, dram_timing
from config.files import GEN_DIR

import configparser
import os

from sys import argv

####################################################################
####################################################################

def multiply_per_core(cfg, llc_stat_name: str):
    per_core_name = f'{llc_stat_name}_per_core'
    cfg['LLC'][llc_stat_name] = str(int(cfg['LLC'][per_core_name]) * int(cfg['CORE']['num_threads']))

####################################################################
####################################################################

build_id = argv[1]
config_file = argv[2]

os.system(fr'rm -rf _generated/{build_id}; mkdir -p _generated/{build_id}')

cfg = configparser.ConfigParser()
cfg.read(config_file)

caches = ['L1i', 'L1d', 'L2', 'LLC', 'iTLB', 'dTLB', 'L2TLB']

# Create sections if they don't exist.
sections = ['SYSTEM', 'CORE', *caches, 'DRAM', 'OS']
for sec in sections:
    if sec not in cfg:
        cfg[sec] = {}

validate_system_section(cfg['SYSTEM'])
validate_core_section(cfg['CORE'])

if 'size_kb_per_core' in cfg['LLC']:
    multiply_per_core(cfg, 'size_kb')
if 'num_mshr_per_core' in cfg['LLC']:
    multiply_per_core(cfg, 'num_mshr')

for c in caches:
    validate_cache_section(cfg[c])
validate_dram_section(cfg['DRAM'])
validate_os_section(cfg['OS'])

constants.write(cfg, build_id)
memsys.write(cfg, build_id)
globs.write(cfg, build_id)
sim.write(cfg, build_id)
dram_timing.write(cfg, build_id)

####################################################################
####################################################################
# Create files for use with CMAKE

# Core model:
with open(f'{GEN_DIR}/{build_id}/core_model.txt', 'w') as wr:
    wr.write(cfg['SYSTEM']['model'])
with open(f'{GEN_DIR}/{build_id}/trace_format.txt', 'w') as wr:
    wr.write(cfg['SYSTEM']['trace_format'])
# Custom defines:
custom_defines = ''
defined_values = cfg['SYSTEM']['defines'].split(',')
for v in defined_values:
    if len(v) == 0:
        continue
    dat = v.split('=')
    if len(dat) == 1:
        custom_defines += f'-D{dat[0]} '
    else:
        custom_defines += f'-D{dat[0]}={dat[1]} '
with open(f'{GEN_DIR}/{build_id}/defines.txt', 'w') as wr:
    wr.write(custom_defines)


####################################################################
####################################################################

if '-b' in argv or '--make-build-now' in argv:
    cmake_build = build_id.upper()
    if os.path.exists(f'builds/{cmake_build}'):
        os.system(f'rm -rf builds/{cmake_build}')
    os.system(f'''mkdir builds/{cmake_build} && cd builds/{cmake_build}
cmake ../.. -DCMAKE_BUILD_TYPE=Release -DBUILD_ID={build_id}
make -j8''')

####################################################################
####################################################################
