/*
 *  author: Suhas Vittal
 *  date:   11 December 2024
 * */

#ifndef SIMPLE_MODEL_OS_h
#define SIMPLE_MODEL_OS_h

#include "os/free_list.h"

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
    
    uint64_t translate_lineaddr(uint64_t, uint8_t coreid);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // SIMPLE_MODEL_OS_h
