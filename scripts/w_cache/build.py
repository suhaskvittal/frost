''' 
author: Suhas Vittal
'''

############################################################
############################################################

from sys import argv
import os

############################################################
############################################################

BASE_FOLDER = 'ini/simple_core/w_cache'

for folder in ['ddr3_comparison', 'main', 'vwq_eval']:
    for f in os.listdir(f'{BASE_FOLDER}/{folder}'):
        name = f[:f.find('.ini')]
        os.system(f'python config.py {name} {BASE_FOLDER}/{folder}/{f} -b')

############################################################
############################################################

