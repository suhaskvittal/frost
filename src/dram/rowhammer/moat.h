/*
 *  author: Suhas Vittal
 *  date:   22 February 2025
 * */

#ifndef DRAM_ROWHAMMER_MOAT_h
#define DRAM_ROWHAMMER_MOAT_h

#include "dram/rowhammer/base.h"
#include "dram/rowhammer/prac.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

extern size_t OPT_AGGRESSOR_MITIGATION_RATE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class MOAT : public RowhammerDefenseBase
{
public:
    const int eligi_threshold;
    const int alert_threshold;
protected:
    using prac_impl = PRAC<true>;
    using tracked_row_type = std::optional<size_t>;

    prac_impl        prac_{};
    tracked_row_type tracked_row_{};

    size_t mitig_trefi_counter_ =0;
public:
    MOAT(void);

    void update_on_command(const DRAMCommand&) override;
    void handle_refresh(size_t rbegin, size_t rend) override;
    void handle_rfm(void) override;
    /*
     * All other functions can be inlined:
     * */
    inline bool check_if_alert_needed(void) const override
    {
        return tracked_row_.has_value() && prac_.ctrs.at(tracked_row_.value()) >= alert_threshold;
    }

    inline bool precharge_do_counter_update(void) override
    {
        return true;
    }
protected:
    void do_aggressor_mitigation(size_t r);
    /*
     * `update_tracked_row` is a bit messy and not easy on the eyes... so I placed it in an inlined function
     * so we can just call this instead.
     * */
    inline void update_tracked_row(size_t r, size_t row_ctr)
    {
        if (row_ctr >= eligi_threshold && (!tracked_row_.has_value() || row_ctr >= prac_.ctrs[tracked_row_.value()]))
            tracked_row_ = r;
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void compute_moat_alert_threshold(int trh);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_ROWHAMMER_MOAT_h
