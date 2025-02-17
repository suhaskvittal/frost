'''
    author: Suhas Vittal
    date:   31 December 2024
'''

import os

from sys import argv

page_mode = argv[1]

prefix = 'op' if page_mode == 'open' else 'cp'
ini_folder = f'ini/simple_core/w_mgt/{prefix}'

ini_list = [f for f in os.listdir(ini_folder) if f.endswith('.ini')]
for f in ini_list:
    build_name = f[:f.find('.ini')]
    build_dir = f'{prefix}_{build_name}'.upper()
    if not os.path.isdir(f'builds/{build_dir}'):
        os.system(f'python config.py {prefix}_{build_name} {ini_folder}/{f} -b')
