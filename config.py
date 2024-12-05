'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from config.validate import *
from config import constants, memsys, globs, sim, dram_timing

import configparser
import os

from sys import argv

####################################################################
####################################################################

build_id = argv[1]
config_file = argv[2]

os.system(fr'rm -rf _generated/{build_id}; mkdir -p _generated/{build_id}')

cfg = configparser.ConfigParser()
cfg.read(config_file)

caches = ['L1i', 'L1d', 'L2', 'LLC']

validate_core_section(cfg['CORE'])
# For the LLC config, if `size_kb_per_core` is specified,
# add `size_kb` now.
if 'size_kb_per_core' in cfg['LLC']:
    cfg['LLC']['size_kb'] = str(int(cfg['LLC']['size_kb_per_core']) * int(cfg['CORE']['num_threads']))
for c in caches:
    validate_cache_section(cfg[c])
validate_dram_section(cfg['DRAM'])

constants.write(cfg, build_id)
memsys.write(cfg, build_id)
globs.write(cfg, build_id)
sim.write(cfg, build_id)
dram_timing.write(cfg, build_id)

####################################################################
####################################################################
