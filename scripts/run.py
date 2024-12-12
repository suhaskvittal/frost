'''
    author: Suhas Vittal
'''

import os
from sys import argv

def get_name(suite, filename):
    if suite == 'spec2017_4xx' or suite == 'spec2017_6xx':
        left, right = filename.find('.')+1, filename.find('_s')
    elif suite == 'ligra':
        return filename
    elif suite == 'stream':
        left, right = 0, filename.find('.')
    elif suite == 'parsec':
        return filename
    return filename[left:right]

suite = argv[1]
build = argv[2]
inst_sim = argv[3]
inst_warmup = argv[4]

benchmarks = [f for f in os.listdir(f'TRACES/{suite}') if f.endswith('.xz')]

os.system(f'mkdir -p out/{suite}/{build}')

for b in benchmarks:
    name = get_name(suite, b)
    print(f'BENCHMARK {b}')
    cmd = f'./builds/{build}/sim TRACES/{suite}/{b} -s {inst_sim} -w {inst_warmup}'
    print(f'\tRunning build {build}:\t{cmd}')
    os.system(f'sbatch -N1 --ntasks-per-node 1 --account=gts-mqureshi4-rg -t8:00:00 -o out/{suite}/{build}/{name}.out --wrap=\"{cmd}\"')
