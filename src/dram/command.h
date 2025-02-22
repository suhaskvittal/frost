/*
 *  author: Suhas Vittal
 *  date:   15 December 2024
 * */

#ifndef DRAM_COMMAND_h
#define DRAM_COMMAND_h

#include <cstdint>
#include <iosfwd>
#include <string>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct DRAMCommand
{
    enum class Type
    {
        // Column commands:
        READ,
        WRITE,
        // Row commands:
        ACTIVATE,
        PRECHARGE,
        INVALID
    };

    uint64_t address;
    Type     type =Type::INVALID;
    bool     autopre =false;
    bool     counter_update =false;

    inline bool is_read(void) const
    {
        return type == Type::READ;
    }

    inline bool is_write(void) const
    {
        return type == Type::WRITE;
    }

    inline bool is_cas(void) const
    {
        return type == Type::READ || type == Type::WRITE;
    }

    inline bool is_act(void) const
    {
        return type == Type::ACTIVATE;
    }

    inline bool is_pre(void) const
    {
        return type == Type::PRECHARGE || autopre;
    }

    inline bool is_pre_only(void) const
    {
        return type == Type::PRECHARGE;
    }

    inline bool is_invalid(void) const
    {
        return type == Type::INVALID;
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

std::string   cmd_string(const DRAMCommand&);
std::ostream& operator<<(std::ostream&, const DRAMCommand&);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_COMMAND_h

