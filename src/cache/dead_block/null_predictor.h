/*
 *  author: Suhas Vittal
 *  date:   5 February 2025
 * */

#ifndef CACHE_DEAD_BLOCK_BASE_PREDICTOR_h
#define CACHE_DEAD_BLOCK_BASE_PREDICTOR_h

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct NoDeadBlockPredictor : DeadBlockPredictorBase
{
    bool predict_if_dead(const Transaction&) const override { return false; }
    void update_on_probe_or_fill(const Transaction&) override {}
    void update_on_mark_dirty(const Transaction&) override {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_DEAD_BLOCK_BASE_PREDICTOR_h
