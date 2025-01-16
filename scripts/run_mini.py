
import os

workloads = [
    'lbm_17',
    'fotonik3d_17',
    'xz_17',
    'bwaves_17',
    'cactuBSSN_17'
]

args = ' '.join(argv[1:])

for w in workloads:
    print(f'source ~/.bashrc && ./sim ../../TRACES/mtf/spec2017/{w}.mtf.gz -s 100000000 -w 0 {args} > {w}.out')
