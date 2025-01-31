'''
    author: Suhas Vittal
    date:   24 January 2025

    Porting script for champsim traces. Note that this just dumps out the
    commands that should be executed.
'''

import os

#######################################################################################
#######################################################################################

SPEC_TRACES = [ f'/traces/champsim/{f}' for f in os.listdir('/traces/champsim') if f.endswith('.xz') ]

LIGRA_TRACES = [
    '/traces/champsim/Ligra/ligra_BFS-Bitvector.com-lj.ungraph.gcc_6.3.0_O3.drop_500M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_BFS.com-lj.ungraph.gcc_6.3.0_O3.drop_500M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_CF.com-lj.ungraph.gcc_6.3.0_O3.drop_2500M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_Components.com-lj.ungraph.gcc_6.3.0_O3.drop_750M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_Components-Shortcut.com-lj.ungraph.gcc_6.3.0_O3.drop_750M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_PageRankDelta.com-lj.ungraph.gcc_6.3.0_O3.drop_6000M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_Radii.com-lj.ungraph.gcc_6.3.0_O3.drop_750M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_Triangle.com-lj.ungraph.gcc_6.3.0_O3.drop_750M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_BC.com-lj.ungraph.gcc_6.3.0_O3.drop_15500M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_BellmanFord.com-lj.ungraph.gcc_6.3.0_O3.drop_33750M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_BFSCC.com-lj.ungraph.gcc_6.3.0_O3.drop_750M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_MIS.com-lj.ungraph.gcc_6.3.0_O3.drop_750M.length_250M.champsimtrace.xz',
    '/traces/champsim/Ligra/ligra_PageRank.com-lj.ungraph.gcc_6.3.0_O3.drop_79500M.length_250M.champsimtrace.xz',
]

PARSEC_TRACES = [
    '/traces/champsim/PARSEC-2.1/parsec_2.1.canneal.simlarge.prebuilt.drop_500M.length_250M.champsimtrace.xz',
    '/traces/champsim/PARSEC-2.1/parsec_2.1.facesim.simlarge.prebuilt.drop_21500M.length_250M.champsimtrace.xz',
    '/traces/champsim/PARSEC-2.1/parsec_2.1.fluidanimate.simlarge.prebuilt.drop_9500M.length_250M.champsimtrace.xz',
    '/traces/champsim/PARSEC-2.1/parsec_2.1.raytrace.simlarge.prebuilt.drop_23750M.length_250M.champsimtrace.xz',
    '/traces/champsim/PARSEC-2.1/parsec_2.1.streamcluster.simlarge.prebuilt.drop_4750M.length_250M.champsimtrace.xz'
]

#######################################################################################
#######################################################################################

PORTING_EXECUTABLE = './build/port_from_champsim'
OUTPUT_DIRECTORY = '../TRACES'

#######################################################################################
#######################################################################################

def get_spec_name(file):
    path_parts = file.split('/')
    filename = path_parts[-1]
    left = filename.find('.')+1
    right = filename.find('_s')
    return filename[left:right]

def get_ligra_name(file):
    path_parts = file.split('/')
    filename = path_parts[-1]
    left = filename.find('ligra_') + len('ligra_')
    right = filename.find('.com')
    return filename[left:right].lower()

def get_parsec_name(file):
    path_parts = file.split('/')
    filename = path_parts[-1]
    left = filename.find('parsec_2.1.') + len('parsec_2.1.')
    right = filename.find('.simlarge')
    return filename[left:right]

#######################################################################################
#######################################################################################

def convert(output_dir: str, trace_list: list[str], name_function, fmt: str):
    for t in trace_list:
        if not os.path.isdir(f'{OUTPUT_DIRECTORY}/{fmt}/{output_dir}'):
            os.mkdir(f'{OUTPUT_DIRECTORY}/{fmt}/{output_dir}')
        name = name_function(t)
        out = f'{OUTPUT_DIRECTORY}/{fmt}/{output_dir}/{name}.{fmt}.gz'
        print(f'{PORTING_EXECUTABLE} {t} {out}')

#######################################################################################
#######################################################################################

from sys import argv

if len(argv) > 1:
    fmt = argv[1]
else:
    fmt = 'mtf'

if not os.path.isdir(f'{OUTPUT_DIRECTORY}/{fmt}'):
    os.mkdir(f'{OUTPUT_DIRECTORY}/{fmt}')

#convert('spec', SPEC_TRACES, get_spec_name, fmt)
convert('ligra', LIGRA_TRACES, get_ligra_name, fmt)
#convert('parsec', PARSEC_TRACES, get_parsec_name, fmt)

#######################################################################################
#######################################################################################
