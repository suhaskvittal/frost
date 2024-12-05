'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from config.validate import *
from config import constants, memsys, globs

import configparser

####################################################################
####################################################################

cfg = configparser.ConfigParser()
cfg.read('example.ini')

caches = ['L1i', 'L1d', 'L2', 'LLC']

validate_core_section(cfg['CORE'])
for c in caches:
    validate_cache_section(cfg[c])
validate_dram_section(cfg['DRAM'])

constants.write(cfg)
memsys.write(cfg)
globs.write(cfg)

####################################################################
####################################################################
