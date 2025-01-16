
import os
from sys import argv

workloads = [
    'lbm_17',
    'fotonik3d_17',
    'xz_17',
    'bwaves_17',
    'cactuBSSN_17'
]

suffix = argv[1]
args = ' '.join(argv[2:])

for w in workloads:
    print(f'source ~/.bashrc && ./sim ../../TRACES/mtf/spec2017/{w}.mtf.gz -s 100000000 -w 0 {args} > {w}_{suffix}.out')
