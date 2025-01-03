'''
    author: Suhas Vittal
    date:   31 December 2024
'''

import os

ini_list = [f for f in os.listdir('ini/simple_core/w_mgt') if f.endswith('.ini')]
for f in ini_list:
    build_name = f[:f.find('.ini')]
    os.system(f'python config.py {build_name} ini/simple_core/w_mgt/{f} -b')
