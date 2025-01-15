#ifndef __COMMON_H
#define __COMMON_H

#include <stdint.h>
#include <iostream>
#include <vector>
/*
 * Models precharge delays due to PRAC (PRECHARGE2)
 * */
#define PRAC_IMPL_ONLY_DELAY 1
/*
 * Models MOAT
 * */
#define PRAC_IMPL_MOAT 2
/*
 * Models MOAT with probabilistic precharge.
 * */
#define PRAC_IMPL_PAC 3
/*
 * Models MOPAC, where PRAC updates are done on REF
 * or when an ABO occurs.
 * */
#define PRAC_IMPL_MOPAC 4

namespace dramsim3 {

struct Address {
    Address()
        : channel(-1), rank(-1), bankgroup(-1), bank(-1), row(-1), column(-1) {}
    Address(int channel, int rank, int bankgroup, int bank, int row, int column)
        : channel(channel),
          rank(rank),
          bankgroup(bankgroup),
          bank(bank),
          row(row),
          column(column) {}
    Address(const Address& addr)
        : channel(addr.channel),
          rank(addr.rank),
          bankgroup(addr.bankgroup),
          bank(addr.bank),
          row(addr.row),
          column(addr.column) {}
    int channel;
    int rank;
    int bankgroup;
    int bank;
    int row;
    int column;
};

inline uint32_t ModuloWidth(uint64_t addr, uint32_t bit_width, uint32_t pos) {
    addr >>= pos;
    auto store = addr;
    addr >>= bit_width;
    addr <<= bit_width;
    return static_cast<uint32_t>(store ^ addr);
}

// extern std::function<Address(uint64_t)> AddressMapping;
int GetBitInPos(uint64_t bits, int pos);
// it's 2017 and c++ std::string still lacks a split function, oh well
std::vector<std::string> StringSplit(const std::string& s, char delim);
template <typename Out>
void StringSplit(const std::string& s, char delim, Out result);

int LogBase2(int power_of_two);
void AbruptExit(const std::string& file, int line);
bool DirExist(std::string dir);

enum class CommandType {
    READ,
    READ_PRECHARGE,
    WRITE,
    WRITE_PRECHARGE,
    ACTIVATE,
    PRECHARGE,
    /*
     * PRAC precharge: with ctr update
     * */
    READ_PRECHARGE2,
    WRITE_PRECHARGE2,
    PRECHARGE2,
    /*
     * REF and RFM
     * */
    REFRESH_BANK,
    REFsb,
    REFab,
    SREF_ENTER,
    SREF_EXIT,
    RFMsb, // [RFM] Same Bank Refresh
    RFMab, // [RFM] All Bank Refresh
    SIZE
};

std::string CommandTypeToString(CommandType cmd_type);

struct Command {
    Command() : cmd_type(CommandType::SIZE), hex_addr(0) {}
    Command(CommandType cmd_type, const Address& addr, uint64_t hex_addr)
        : cmd_type(cmd_type), addr(addr), hex_addr(hex_addr) {}
    // Command(const Command& cmd) {}

    bool IsValid() const { return cmd_type != CommandType::SIZE; }
    bool IsRefresh() const {
        return cmd_type == CommandType::REFab ||
               cmd_type == CommandType::REFsb ||
               cmd_type == CommandType::REFRESH_BANK;
    }
    bool IsRFM() const {
        return cmd_type == CommandType::RFMab || cmd_type == CommandType::RFMsb;
    }
    bool IsRead() const {
        return cmd_type == CommandType::READ
               || cmd_type == CommandType::READ_PRECHARGE
               || cmd_type == CommandType::READ_PRECHARGE2 ;
    }
    bool IsWrite() const {
        return cmd_type == CommandType::WRITE
               || cmd_type == CommandType::WRITE_PRECHARGE
               || cmd_type == CommandType::WRITE_PRECHARGE2;
    }
    bool IsReadWrite() const { return IsRead() || IsWrite(); }
    
    bool IsSbCMD() const {
        return cmd_type == CommandType::RFMsb ||
               cmd_type == CommandType::REFsb;
    }
    
    bool IsRankCMD() const {
        return cmd_type == CommandType::REFab ||
               cmd_type == CommandType::SREF_ENTER ||
               cmd_type == CommandType::SREF_EXIT ||
               cmd_type == CommandType::RFMab;
    }
    CommandType cmd_type;
    Address addr;
    uint64_t hex_addr;

    int Channel() const { return addr.channel; }
    int Rank() const { return addr.rank; }
    int Bankgroup() const { return addr.bankgroup; }
    int Bank() const { return addr.bank; }
    int Row() const { return addr.row; }
    int Column() const { return addr.column; }

    friend std::ostream& operator<<(std::ostream& os, const Command& cmd);
};

struct Transaction {
    Transaction() {}
    Transaction(uint64_t addr, bool is_write)
        : addr(addr),
          added_cycle(0),
          complete_cycle(0),
          is_write(is_write) {}
    Transaction(const Transaction& tran)
        : addr(tran.addr),
          added_cycle(tran.added_cycle),
          complete_cycle(tran.complete_cycle),
          is_write(tran.is_write) {}
    uint64_t addr;
    uint64_t added_cycle;
    uint64_t complete_cycle;
    bool is_write;

    friend std::ostream& operator<<(std::ostream& os, const Transaction& trans);
    friend std::istream& operator>>(std::istream& is, Transaction& trans);
};

}  // namespace dramsim3
#endif
