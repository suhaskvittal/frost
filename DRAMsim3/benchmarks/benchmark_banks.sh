#!/bin/bash
set -x
request_count=1000000
cycle_count=10000000

dramsim3_dir="/home/hritvik/abrfm/DRAMsim3"
abrfm_dir="/home/hritvik/abrfm"

function run_benchmark {
    bank=$1
    dir="./bank${bank}/norfm"

    mkdir -p $dir
    cd $dir
    python3 ${dramsim3_dir}/scripts/trace_gen.py -s c -f dramsim3 -n $request_count -b $bank

    ${dramsim3_dir}/build/dramsim3main ${dramsim3_dir}/configs/DDR5_32Gb_x4_4800_norfm.ini -c $cycle_count -t dramsim3_custom_i10_n${request_count}_rw2_b${bank}.trace

    python3 ${abrfm_dir}/experiment/triplets.py DDR5_baselinech_0cmd.trace > stats.txt
    python3 ${abrfm_dir}/experiment/triplets.py DDR5_baselinech_1cmd.trace >> stats.txt
    echo "Bank $bank done."
    cd -
}  

function run_benchmark_rfm {
    bank=$1
    dir="./bank${bank}/rfm"

    mkdir -p $dir
    cd $dir
    python3 ${dramsim3_dir}/scripts/trace_gen.py -s c -f dramsim3 -n $request_count -b $bank

    ${dramsim3_dir}/build/dramsim3main ${dramsim3_dir}/configs/DDR5_32Gb_x4_4800.ini -c $cycle_count -t dramsim3_custom_i10_n${request_count}_rw2_b${bank}.trace

    python3 ${abrfm_dir}/experiment/triplets.py DDR5_baselinech_0cmd.trace > stats.txt
    python3 ${abrfm_dir}/experiment/triplets.py DDR5_baselinech_1cmd.trace >> stats.txt
    echo "Bank $bank done."
    cd -
}  

for bank in {1..32}
do
    run_benchmark $bank &
    run_benchmark_rfm $bank &
done

wait