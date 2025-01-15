#include "configuration.h"

#include <vector>

#ifdef THERMAL
#include <math.h>
#endif  // THERMAL

namespace dramsim3 {

Config::Config(std::string config_file, std::string out_dir)
    : output_dir(out_dir), reader_(new INIReader(config_file)) {
    if (reader_->ParseError() < 0) {
        std::cerr << "Can't load config file - " << config_file << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    // The initialization of the parameters has to be strictly in this order
    // because of internal dependencies
    InitSystemParams();
    InitDRAMParams();
    CalculateSize();
    SetAddressMapping();
    InitTimingParams();
    InitPowerParams();
    InitOtherParams();
    InitRFMParams();
    InitALERTParams();
    InitMOATParams();
    InitMIRZAParams();
#ifdef THERMAL
    InitThermalParams();
#endif  // THERMAL
    delete (reader_);
}

Address Config::AddressMapping(uint64_t hex_addr) const {
    hex_addr >>= shift_bits;
    int channel = (hex_addr >> ch_pos) & ch_mask;
    int rank = (hex_addr >> ra_pos) & ra_mask;
    int bg = (hex_addr >> bg_pos) & bg_mask;
    int ba = (hex_addr >> ba_pos) & ba_mask;
    int ro = (hex_addr >> ro_pos) & ro_mask;
    int co;
    if (mop_enabled)
    {
        int hi = (hex_addr >> hi_pos) & hi_mask;
        int lo = (hex_addr >> lo_pos) & lo_mask;
        co = (hi << LogBase2(mop_size)) | lo;
    }
    else
    {
        co = (hex_addr >> co_pos) & co_mask;
    }
    return Address(channel, rank, bg, ba, ro, co);
}

void Config::CalculateSize() {
    // calculate rank and re-calculate channel_size
    devices_per_rank = bus_width / device_width;
    int page_size = columns * device_width / 8;  // page size in bytes
    int megs_per_bank = page_size * (rows / 1024) / 1024;
    int megs_per_rank = megs_per_bank * banks * devices_per_rank;

    if (megs_per_rank > channel_size) {
        std::cout << "WARNING: Cannot create memory system of size "
                  << channel_size
                  << "MB with given device choice! Using default size "
                  << megs_per_rank << " instead!" << std::endl;
        ranks = 1;
        channel_size = megs_per_rank;
    } else {
        ranks = channel_size / megs_per_rank;
        channel_size = ranks * megs_per_rank;
    }
    return;
}

DRAMProtocol Config::GetDRAMProtocol(std::string protocol_str) {
    std::map<std::string, DRAMProtocol> protocol_pairs = {
        {"DDR3", DRAMProtocol::DDR3},     {"DDR4", DRAMProtocol::DDR4}, {"DDR5", DRAMProtocol::DDR5},
        {"GDDR5", DRAMProtocol::GDDR5},   {"GDDR5X", DRAMProtocol::GDDR5X},  {"GDDR6", DRAMProtocol::GDDR6},
        {"LPDDR", DRAMProtocol::LPDDR},   {"LPDDR3", DRAMProtocol::LPDDR3},
        {"LPDDR4", DRAMProtocol::LPDDR4}, {"HBM", DRAMProtocol::HBM},
        {"HBM2", DRAMProtocol::HBM2},     {"HMC", DRAMProtocol::HMC}};

    if (protocol_pairs.find(protocol_str) == protocol_pairs.end()) {
        std::cout << "Unkwown/Unsupported DRAM Protocol: " << protocol_str
                  << " Aborting!" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    return protocol_pairs[protocol_str];
}

int Config::GetInteger(const std::string& sec, const std::string& opt,
                       int default_val) const {
    return static_cast<int>(reader_->GetInteger(sec, opt, default_val));
}

void Config::InitDRAMParams() {
    const auto& reader = *reader_;
    protocol =
        GetDRAMProtocol(reader.Get("dram_structure", "protocol", "DDR3"));
    bankgroups = GetInteger("dram_structure", "bankgroups", 2);
    banks_per_group = GetInteger("dram_structure", "banks_per_group", 2);
    bool bankgroup_enable =
        reader.GetBoolean("dram_structure", "bankgroup_enable", true);
    // GDDR5/6 can chose to enable/disable bankgroups
    if (!bankgroup_enable) {  // aggregating all banks to one group
        banks_per_group *= bankgroups;
        bankgroups = 1;
    }
    banks = bankgroups * banks_per_group;
    rows = GetInteger("dram_structure", "rows", 1 << 16);
    columns = GetInteger("dram_structure", "columns", 1 << 10);
    device_width = GetInteger("dram_structure", "device_width", 8);
    BL = GetInteger("dram_structure", "BL", 8);
    num_dies = GetInteger("dram_structure", "num_dies", 1);
    // HBM specific parameters
    enable_hbm_dual_cmd =
        reader.GetBoolean("dram_structure", "hbm_dual_cmd", true);
    enable_hbm_dual_cmd &= IsHBM();  // Make sure only HBM enables this
    // HMC specific parameters
    num_links = GetInteger("hmc", "num_links", 4);
    link_width = GetInteger("hmc", "link_width", 16);
    link_speed = GetInteger("hmc", "link_speed", 15000);  //MHz
    block_size = GetInteger("hmc", "block_size", 64);
    xbar_queue_depth = GetInteger("hmc", "xbar_queue_depth", 16);
    if (IsHMC()) {
        // the BL for HMC is determined by max block_size, which is a multiple
        // of 32B, each "device" transfer 32b per half cycle therefore BL is 8
        // for 32B block size
        BL = block_size * 8 / device_width;
    }
    // set burst cycle according to protocol
    // We use burst_cycle for timing and use BL for capacity calculation
    // BL = 0 simulate perfect BW
    if (protocol == DRAMProtocol::GDDR5) {
        burst_cycle = (BL == 0) ? 0 : BL / 4;
        BL = (BL == 0) ? 8 : BL;
    } else if (protocol == DRAMProtocol::GDDR5X) {
        burst_cycle = (BL == 0) ? 0 : BL / 8;
        BL = (BL == 0) ? 8 : BL;
    } else if (protocol == DRAMProtocol::GDDR6){
        burst_cycle = (BL == 0) ? 0 : BL / 16;
        BL = (BL == 0 ) ? 8 : BL;
    } else {
        burst_cycle = (BL == 0) ? 0 : BL / 2;
        BL = (BL == 0) ? (IsHBM() ? 4 : 8) : BL;
    }
    // every protocol has a different definition of "column",
    // in DDR3/4, each column is exactly device_width bits,
    // but in GDDR5, a column is device_width * BL bits
    // and for HBM each column is device_width * 2 (prefetch)
    // as a result, different protocol has different method of calculating
    // page size, and address mapping...
    // To make life easier, we regulate the use of the term "column"
    // to only represent physical column (device width)
    if (IsGDDR()) {
        columns *= BL;
    } else if (IsHBM()) {
        columns *= 2;
    }
    return;
}

void Config::InitOtherParams() {
    const auto& reader = *reader_;
    epoch_period = GetInteger("other", "epoch_period", 100000);
    // determine how much output we want:
    // -1: no file output at all (NOT implemented yet)
    // 0: no epoch file output, only outputs the summary in the end
    // 1: default value, adds epoch CSV output on level 0
    // 2: adds histogram outputs in a different CSV format
    output_level = reader.GetInteger("other", "output_level", 1);
    // Other Parameters
    // give a prefix instead of specify the output name one by one...
    // this would allow outputing to a directory and you can always override
    // these values
    if (!DirExist(output_dir)) {
        std::cout << "WARNING: Output directory " << output_dir
                  << " not exists! Using current directory for output!"
                  << std::endl;
        output_dir = "./";
    } else {
        output_dir = output_dir + "/";
    }
    output_prefix =
        output_dir + reader.Get("other", "output_prefix", "dramsim3");
    json_stats_name = output_prefix + ".json";
    json_epoch_name = output_prefix + "epoch.json";
    txt_stats_name = output_prefix + ".txt";
    return;
}

void Config::InitPowerParams() {
    const auto& reader = *reader_;
    // Power-related parameters
    double VDD = reader.GetReal("power", "VDD", 1.2);
    double IDD0 = reader.GetReal("power", "IDD0", 48);
    double IDD2P = reader.GetReal("power", "IDD2P", 25);
    double IDD2N = reader.GetReal("power", "IDD2N", 34);
    // double IDD3P = reader.GetReal("power", "IDD3P", 37);
    double IDD3N = reader.GetReal("power", "IDD3N", 43);
    double IDD4W = reader.GetReal("power", "IDD4W", 123);
    double IDD4R = reader.GetReal("power", "IDD4R", 135);
    double IDD5AB = reader.GetReal("power", "IDD5AB", 250);  // all-bank ref
    double IDD5PB = reader.GetReal("power", "IDD5PB", 5);    // per-bank ref
    double IDD6x = reader.GetReal("power", "IDD6x", 31);

    // energy increments per command/cycle, calculated as voltage * current *
    // time(in cycles) units are V * mA * Cycles and if we convert cycles to ns
    // then it's exactly pJ in energy and because a command take effects on all
    // devices per rank, also multiply that number
    double devices = static_cast<double>(devices_per_rank);
    act_energy_inc =
        VDD * (IDD0 * tRC - (IDD3N * tRAS + IDD2N * tRP)) * devices;
    read_energy_inc = VDD * (IDD4R - IDD3N) * burst_cycle * devices;
    write_energy_inc = VDD * (IDD4W - IDD3N) * burst_cycle * devices;
    ref_energy_inc = VDD * (IDD5AB - IDD3N) * tRFC * devices;
    refb_energy_inc = VDD * (IDD5PB - IDD3N) * tRFCb * devices;
    // the following are added per cycle
    act_stb_energy_inc = VDD * IDD3N * devices;
    pre_stb_energy_inc = VDD * IDD2N * devices;
    pre_pd_energy_inc = VDD * IDD2P * devices;
    sref_energy_inc = VDD * IDD6x * devices;
    return;
}

void Config::InitSystemParams() {
    const auto& reader = *reader_;
    channel_size = GetInteger("system", "channel_size", 1024);
    channels = GetInteger("system", "channels", 1);
    bus_width = GetInteger("system", "bus_width", 64);
    address_mapping = reader.Get("system", "address_mapping", "chrobabgraco");
    mop_size = reader.GetInteger("system", "mop_size", 0);
    queue_structure = reader.Get("system", "queue_structure", "PER_BANK");
    
    std::string row_buf_policy_str = reader.Get("system", "row_buf_policy", "OPEN_PAGE");
    std::cout << "[DRAM] Row Buffer Policy: " << row_buf_policy_str << std::endl;
    if (row_buf_policy_str == "OPEN_PAGE") {
        row_buf_policy = RowBufPolicy::OPEN_PAGE;
    } else if (row_buf_policy_str == "CLOSE_PAGE") {
        row_buf_policy = RowBufPolicy::CLOSE_PAGE;
    } else if (row_buf_policy_str == "SOFT_CLOSE_PAGE") {
        row_buf_policy = RowBufPolicy::SOFT_CLOSE_PAGE;
    } else {
        AbruptExit(__FILE__, __LINE__);
    }

    cmd_queue_size = GetInteger("system", "cmd_queue_size", 16);
    trans_queue_size = GetInteger("system", "trans_queue_size", 32);
    unified_queue = reader.GetBoolean("system", "unified_queue", false);
    write_buf_size = GetInteger("system", "write_buf_size", 16);
    
    std::string ref_policy =
        reader.Get("system", "refresh_policy", "RANK_LEVEL_STAGGERED");
    std::cout << "[DRAM] Refresh Policy: " << ref_policy << std::endl;
    if (ref_policy == "RANK_LEVEL_SIMULTANEOUS") {
        refresh_policy = RefreshPolicy::RANK_LEVEL_SIMULTANEOUS;
    } else if (ref_policy == "RANK_LEVEL_STAGGERED") {
        refresh_policy = RefreshPolicy::RANK_LEVEL_STAGGERED;
    } else if (ref_policy == "BANKSET_LEVEL_STAGGERED") {
        refresh_policy = RefreshPolicy::BANKSET_LEVEL_STAGGERED;
    } else if (ref_policy == "BANK_LEVEL_STAGGERED") {
        refresh_policy = RefreshPolicy::BANK_LEVEL_STAGGERED;
    } else if (ref_policy == "NO_REFRESH") {
        refresh_policy = RefreshPolicy::NO_REFRESH;
    } else {
        AbruptExit(__FILE__, __LINE__);
    }

    enable_self_refresh =
        reader.GetBoolean("system", "enable_self_refresh", false);
    sref_threshold = GetInteger("system", "sref_threshold", 1000);
    aggressive_precharging_enabled =
        reader.GetBoolean("system", "aggressive_precharging_enabled", false);

    moat_ath = GetInteger("system", "MoatATH", 256);
    pac_prob = GetInteger("system", "PacProb", 8);
    mopac_buf_size = GetInteger("system", "MopacBufSize", 5);
    mopac_abo_updates = GetInteger("system", "MopacABOUpdates", 5);
    mopac_ref_updates = GetInteger("system", "MopacREFUpdates", 2);

    wsync_mode = GetInteger("system", "WsyncMode", 0);

    std::cout << "PRAC: [ moat_ath = " << moat_ath << ", (pac) 1/p = " << pac_prob << ", mopac buf size = " 
        << mopac_buf_size << ", updates = " << mopac_ref_updates << ";" << mopac_abo_updates << " ]\n";

    return;
}

#ifdef THERMAL
void Config::InitThermalParams() {
    const auto& reader = *reader_;
    const_logic_power = reader.GetReal("thermal", "const_logic_power", 5.0);
    mat_dim_x = GetInteger("thermal", "mat_dim_x", 512);
    mat_dim_y = GetInteger("thermal", "mat_dim_y", 512);
    // row_tile = GetInteger("thermal", "row_tile", 1));
    num_x_grids = rows / mat_dim_x;
    tile_row_num = rows;

    num_y_grids = columns * device_width / mat_dim_y;
    bank_asr = (double)num_x_grids / num_y_grids;
    row_tile = 1;
    if (bank_asr > 4 && banks_per_group == 1) {
        // YZY: I set the aspect ratio as 4
        // I assume if bank_asr <= 4, the dimension can be corrected by
        // arranging banks/vaults
        while (row_tile * row_tile * 4 < bank_asr) {
            row_tile *= 2;
        }
        // row_tile = num_x_grids / (num_y_grids * 8);
#ifdef DEBUG_OUTPUT
        std::cout << "row_tile = " << row_tile << std::endl;
#endif  // DEBUG_OUTPUT
        num_x_grids = num_x_grids / row_tile;
        tile_row_num = tile_row_num / row_tile;
        num_y_grids = num_y_grids * row_tile;
        bank_asr = (double)num_x_grids / num_y_grids;
    } else {
#ifdef DEBUG_OUTPUT
        std::cout << "No Need to Tile Rows\n";
#endif  // DEBUG_OUTPUT
        loc_mapping = reader.Get("thermal", "loc_mapping", "");
        bank_order = GetInteger("thermal", "bank_order", 1);
        bank_layer_order = GetInteger("thermal", "bank_layer_order", 0);
        num_row_refresh =
            static_cast<int>(ceil(rows / (64 * 1e6 / (tREFI * tCK))));
        chip_dim_x = reader.GetReal("thermal", "chip_dim_x", 0.01);
        chip_dim_y = reader.GetReal("thermal", "chip_dim_y", 0.01);
        amb_temp = reader.GetReal("thermal", "amb_temp", 40);
    }
    return;
}
#endif  // THERMAL

void Config::InitTimingParams() {
    // Timing Parameters
    // TODO there is no need to keep all of these variables, they should
    // just be temporary, ultimately we only need cmd to cmd Timing
    const auto& reader = *reader_;
    tCK = reader.GetReal("timing", "tCK", 1.0);
    AL = GetInteger("timing", "AL", 0);
    CL = GetInteger("timing", "CL", 12);
    CWL = GetInteger("timing", "CWL", 12);
    tCCD_L = GetInteger("timing", "tCCD_L", 6);
    tCCD_S = GetInteger("timing", "tCCD_S", 4);
    tRTRS = GetInteger("timing", "tRTRS", 2);
    tRTP = GetInteger("timing", "tRTP", 5);
    tWTR_L = GetInteger("timing", "tWTR_L", 5);
    tWTR_S = GetInteger("timing", "tWTR_S", 5);
    tWR = GetInteger("timing", "tWR", 10);
    tRP = GetInteger("timing", "tRP", 10);
    tRRD_L = GetInteger("timing", "tRRD_L", 4);
    tRRD_S = GetInteger("timing", "tRRD_S", 4);
    tRAS = GetInteger("timing", "tRAS", 24);
    tRCD = GetInteger("timing", "tRCD", 10);
    tRC = tRAS + tRP;
    tCKE = GetInteger("timing", "tCKE", 6);
    tCKESR = GetInteger("timing", "tCKESR", 12);
    tXS = GetInteger("timing", "tXS", 432);
    tXP = GetInteger("timing", "tXP", 8);
    tREFSBRD = GetInteger("timing", "tREFSBRD", 72);
    tRFC = GetInteger("timing", "tRFC", 74);
    tRFCsb = GetInteger("timing", "tRFCsb", 74);
    tRFCb = GetInteger("timing", "tRFCb", 20);
    tREFI = GetInteger("timing", "tREFI", 7800);
    tREFIsb = GetInteger("timing", "tREFIsb", 1950);
    tREFIb = GetInteger("timing", "tREFIb", 1950);
    tFAW = GetInteger("timing", "tFAW", 50);
    tRPRE = GetInteger("timing", "tRPRE", 1);
    tWPRE = GetInteger("timing", "tWPRE", 1);

    // LPDDR4 and GDDR5/6
    tPPD = GetInteger("timing", "tPPD", 0);

    // GDDR5/6
    t32AW = GetInteger("timing", "t32AW", 330);
    tRCDRD = GetInteger("timing", "tRCDRD", 24);
    tRCDWR = GetInteger("timing", "tRCDWR", 20);

    ideal_memory_latency = GetInteger("timing", "ideal_memory_latency", 10);

    tRP2 = GetInteger("timing", "tRP2", 86);
    tRAS2 = GetInteger("timing", "tRAS2", 38);

    // calculated timing
    RL = AL + CL;
    WL = AL + CWL;
    read_delay = RL + burst_cycle;
    write_delay = WL + burst_cycle;
    return;
}

void Config::SetAddressMapping() {
    // memory addresses are byte addressable, but each request comes with
    // multiple bytes because of bus width, and burst length
    request_size_bytes = bus_width / 8 * BL;
    shift_bits = LogBase2(request_size_bytes);
    std::cout << "[DRAM] Request Size: " << request_size_bytes << " bytes" << std::endl;
    std::cout << "[DRAM] Shift Bits: " << shift_bits << std::endl;
    int col_low_bits = LogBase2(BL);
    int actual_col_bits = LogBase2(columns) - col_low_bits;

    // has to strictly follow the order of chan, rank, bg, bank, row, col
    std::map<std::string, int> field_widths;
    field_widths["ch"] = LogBase2(channels);
    field_widths["ra"] = LogBase2(ranks);
    field_widths["bg"] = LogBase2(bankgroups);
    field_widths["ba"] = LogBase2(banks_per_group);
    field_widths["ro"] = LogBase2(rows);
    field_widths["co"] = actual_col_bits;
    field_widths["hi"] = actual_col_bits - LogBase2(mop_size);
    field_widths["lo"] = LogBase2(mop_size);


    if (address_mapping.size() != 12 and address_mapping.size() != 14) {
        std::cerr << "Unknown address mapping (6 fields each 2 chars required) [ch, ra, bg, ba, ro, co]"
                  << "\n or (7 fields each 2 chars required) [ch, ra, bg, ba, ro, hi, lo]" 
                  << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    if (address_mapping.size() == 14)
    {
        mop_enabled = true;
        std::cout << "[DRAM MOP Enabled] MOP Size: " << mop_size << std::endl;
        std::cout << "Expected: rohirababgchlo, Actual: " << address_mapping << std::endl;
    }

    // // get address mapping position fields from config
    // // each field must be 2 chars
    std::vector<std::string> fields;
    for (size_t i = 0; i < address_mapping.size(); i += 2)
    {
        std::string token = address_mapping.substr(i, 2);
        fields.push_back(token);
    }

    std::map<std::string, int> field_pos;
    int pos = 0;
    while (!fields.empty()) {
        auto token = fields.back();
        fields.pop_back();
        if (field_widths.find(token) == field_widths.end()) {
            std::cerr << "Unrecognized field: " << token << std::endl;
            AbruptExit(__FILE__, __LINE__);
        }
        field_pos[token] = pos;
        pos += field_widths[token];
    }

    ch_pos = field_pos.at("ch");
    ra_pos = field_pos.at("ra");
    bg_pos = field_pos.at("bg");
    ba_pos = field_pos.at("ba");
    ro_pos = field_pos.at("ro");
    if (mop_enabled)
    {
        hi_pos = field_pos.at("hi");
        lo_pos = field_pos.at("lo");
    }
    else
    {
        co_pos = field_pos.at("co");
    }
    

    ch_mask = (1 << field_widths.at("ch")) - 1;
    ra_mask = (1 << field_widths.at("ra")) - 1;
    bg_mask = (1 << field_widths.at("bg")) - 1;
    ba_mask = (1 << field_widths.at("ba")) - 1;
    ro_mask = (1 << field_widths.at("ro")) - 1;
    
    if (mop_enabled)
    {
        hi_mask = (1 << field_widths.at("hi")) - 1;
        lo_mask = (1 << field_widths.at("lo")) - 1;
    }
    else
    {
        co_mask = (1 << field_widths.at("co")) - 1;
    }

    std::cout << "[DRAM] Channel Position: " << ch_pos << "| Width: " << field_widths.at("ch") << std::endl;
    std::cout << "[DRAM] Rank Position: " << ra_pos << "| Width: " << field_widths.at("ra") << std::endl;
    std::cout << "[DRAM] BankGroup Position: " << bg_pos << "| Width: " << field_widths.at("bg") << std::endl;
    std::cout << "[DRAM] Bank Position: " << ba_pos << "| Width: " << field_widths.at("ba") << std::endl;
    std::cout << "[DRAM] Row Position: " << ro_pos << "| Width: " << field_widths.at("ro") << std::endl;
    if (mop_enabled)
    {
        std::cout << "[DRAM] High Position: " << hi_pos << "| Width: " << field_widths.at("hi") << std::endl;
        std::cout << "[DRAM] Low Position: " << lo_pos << "| Width: " << field_widths.at("lo") << std::endl;
    }
    else
    {
        std::cout << "[DRAM] Column Position: " << co_pos << "| Width: " << field_widths.at("co") << std::endl;    
    }
}

void Config::InitRFMParams() {
    const auto& reader = *reader_;
    // [RFM] Parameters
    raaimt = reader.GetInteger("rfm", "raaimt", 32);
    raammt = reader.GetInteger("rfm", "raammt", raaimt * 3);

    rfm_raa_decrement = reader.GetInteger("rfm", "rfm_raa_decrement", raaimt);
    ref_raa_decrement = reader.GetInteger("rfm", "ref_raa_decrement", raaimt * 0.5);

    // Should be called after InitTimingParams()
    tRFM = reader.GetInteger("rfm", "tRFM", tRFC);
    tRFMsb = reader.GetInteger("rfm", "tRFMsb", 0.5 * tRFC);

    rfm_mode = reader.GetInteger("rfm", "rfm_mode", 0);

    std::cout << "[RFM] rfm_mode: " << rfm_mode << std::endl;
    if (rfm_mode != 0)
    {
        std::cout << "[RFM] raaimt: " << raaimt << std::endl;
        std::cout << "[RFM] raammt: " << raammt << std::endl;
        std::cout << "[RFM] rfm_raa_decrement: " << rfm_raa_decrement << std::endl;
        std::cout << "[RFM] ref_raa_decrement: " << ref_raa_decrement << std::endl;
        std::cout << "[RFM] tRFM: " << tRFM << std::endl;
        std::cout << "[RFM] tRFMsb: " << tRFMsb << std::endl;
    }

    return;
}

void Config::InitALERTParams() {
    const auto& reader = *reader_;
    // [ALERT] Parameters
    alert_mode = reader.GetInteger("alert", "alert_mode", 0);
    rows_refreshed = rows / reader.GetInteger("alert", "refchunks", 8192);
    tABO = reader.GetInteger("alert", "tABO", 432); // 180ns * 2.4GHz = 432
    ABO_delay_acts = reader.GetInteger("alert", "ABO_delay_acts", 1);


    std::cout << "[ALERT] alert_mode: " << alert_mode << std::endl;
    if (alert_mode != 0)
    {
        std::cout << "[ALERT] rows_refreshed: " << rows_refreshed << std::endl;
        std::cout << "[ALERT] tABO: " << tABO << std::endl;
        std::cout << "[ALERT] ABO_delay_acts: " << ABO_delay_acts << std::endl;
        std::cout << "[RFM] tRFM: " << tRFM << std::endl;
    }

    return;
}


void Config::InitMOATParams() {
    const auto& reader = *reader_;
    // [MOAT] Parameters
    moat_mode = reader.GetInteger("moat", "moat_mode", 0);
    moatth = reader.GetInteger("moat", "moatth", 64);

    std::cout << "[MOAT] moat_mode: " << moat_mode << std::endl;
    if (moat_mode != 0)
    {
        std::cout << "[MOAT] moatth: " << moatth << std::endl;
    }
    return;
}

void Config::InitMIRZAParams() {
    const auto& reader = *reader_;
    // [MIRZA] Parameters
    mirza_mode = reader.GetInteger("mirza", "mirza_mode", 0);
    mirza_groups = reader.GetInteger("mirza", "mirza_groups", 256);
    mirza_groupth = reader.GetInteger("mirza", "mirza_groupth", 1200);
    mirza_qsize = reader.GetInteger("mirza", "mirza_qsize", 4);
    mirza_qth = reader.GetInteger("mirza", "mirza_qth", 40);
    mirza_mintw = reader.GetInteger("mirza", "mirza_mintw", 8);

    std::cout << "[MIRZA] mirza_mode: " << mirza_mode << std::endl;
    if (mirza_mode != 0)
    {
        std::cout << "[MIRZA] mirza_groups: " << mirza_groups << std::endl;
        std::cout << "[MIRZA] mirza_groupth: " << mirza_groupth << std::endl;
        std::cout << "[MIRZA] mirza_qsize: " << mirza_qsize << std::endl;
        std::cout << "[MIRZA] mirza_qth: " << mirza_qth << std::endl;
        std::cout << "[MIRZA] mirza_mintw: " << mirza_mintw << std::endl;
    }
    return;
}

}  // namespace dramsim3
