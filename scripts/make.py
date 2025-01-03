'''
    author: Suhas Vittal
    date:   31 December 2024
'''

import os

builds = os.listdir('builds')

for b in builds:
    os.system(f'cd builds/{b} && make -j4')
