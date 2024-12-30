/*
 *  author: Suhas Vittal
 *  date:   11 December 2024
 * */

#ifndef SIMPLE_MODEL_OS_h
#define SIMPLE_MODEL_OS_h

#include "os/free_list.h"

#include <cstdint>
#include <iosfwd>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class OS
{
public:
    FreeList free_list_{};
private:
    using page_table_t = std::unordered_map<uint64_t, uint64_t>;

    page_table_t pt_;
public:
    OS(void) =default;
    /*
     * `tick` needs to do nothing. This exists for compatibility.
     * */ 
    void tick(void) {}

    uint64_t translate_lineaddr(uint64_t, uint8_t coreid);
    void print_stats(std::ostream&);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // SIMPLE_MODEL_OS_h
