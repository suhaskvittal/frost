'''
    author: Suhas Vittal
    date:   12 December 2024
'''

####################################################################
####################################################################

def is_bar(line):
    return '-----------------------' in line

def parse_line(out: dict, line: str):
    data = line.strip().split()
    out[data[0]] = data[1]

def parse_core_line(out: dict, line: str):
    data = line.strip().split()
    core, stat, value = data[:]
    if core not in out:
        out[core] = {}
    out[core][stat] = value

def parse_dram_line(out: dict, line: str):
    data = line.strip().split()
    dram, stat, acc = data[0], data[1], data[-1]
    data = data[2:-1]
    if dram not in out:
        out[dram] = {}
    out[dram][stat] = {'all': acc}
    for (i, value) in enumerate(data):
        out[dram][stat][f'CHANNEL_{i}'] = value

def parse_table(rd, out: dict, header_line: str, table_name: str, place_in_core_data=False):
    d = {}
    header_data = header_line.strip().split()
    # skip next line -- this is just a bar.
    rd.readline()
    while True:
        line = rd.readline().strip()
        if is_bar(line):
            break
        data = line.split()
        if len(data) == 0:
            continue
        key = data[0]
        values = { header_data[i] : data[i] for i in range(1, len(header_data)) }
        if place_in_core_data:
            ii = key.rfind('_')
            prefix = key[:ii]
            out[prefix][key[ii+1:]] = values
        else:
            d[key] = values
    if not place_in_core_data:
        out[table_name] = d

####################################################################
####################################################################

def read_config_info(rd) -> dict:
    out = {}

    # Search for line beginning with `TRACE` -- we start reading from there.
    line = rd.readline()
    while 'TRACE' not in line:
        line = rd.readline()
    while not is_bar(line):
        parse_line(out, line)
        line = rd.readline()
    # Now, the cache info should be here.
    parse_table(rd, out, rd.readline(), 'CACHE_CONFIG')
    rd.readline() # Skip bar
    # Read the next lines until we hit the address mapping display
    line = rd.readline()
    while 'Address Mapping:' not in line:
        parse_line(out, line)
        line = rd.readline()
    # Now, skip lines until we reach DRAM_TIMING
    while 'DRAM_TIMING' not in line:
        line = rd.readline()
    parse_table(rd, out, line, 'DRAM_TIMING')
    rd.readline() # Now, we should be done.

    return out

def read_results(rd) -> dict:
    out = {}

    # Keep reading until we hit the first bar.
    line = rd.readline()
    while not is_bar(line):
        line = rd.readline()
    # This is the core region: keep reading until we hit DRAM
    line = rd.readline()
    while 'DRAM' not in line:
        if 'CACHE' in line:
            # We have hit the cache results table.
            parse_table(rd, out, line, '', place_in_core_data=True)
        elif not is_bar(line):
            parse_core_line(out, line)
        line = rd.readline()

    # Next data is all dram data. Stop when we hit a line
    while not is_bar(line):
        parse_dram_line(out, line)
        line = rd.readline()
    rd.readline()
    # The remaining data we don't care about.
    return out
    
####################################################################
####################################################################

def read_output_file(filename: str) -> tuple[dict,dict]:
    # A lot of the info is just for our information, so we need
    # to ignore the useless stuff.
    with open(filename, 'r') as rd:
        config = read_config_info(rd)
        results = read_results(rd)
    return config, results

####################################################################
####################################################################
