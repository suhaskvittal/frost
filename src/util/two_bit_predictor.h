/*
 *  author: Suhas Vittal
 *  date:   17 January 2025
 * */

#ifndef UTIL_TWO_BIT_PREDICTOR_h
#define UTIL_TWO_BIT_PREDICTOR_h

#include <cstdint>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class TwoBitPredictor
{
public:
    enum class State { STRONG_YES =3, WEAK_YES =2, WEAK_NO =1, STRONG_NO =0 };
private:
    constexpr static uint8_t DEFAULT_STATE = 0b01;

    uint8_t bits =DEFAULT_STATE;
public:
    inline void promote()
    {
        if (bits < 3)
            ++bits;
    }

    inline void demote()
    {
        if (bits > 0)
            --bits;
    }

    inline State state()
    {
        return static_cast<State>(bits);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif
