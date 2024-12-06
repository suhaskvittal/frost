/*
 *  author: Suhas Vittal
 *  date:   5 December 2024
 * */

#ifndef CACHE_FAST_CONTROL_h
#define CACHE_FAST_CONTROL_h

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * This is an implementation of a cache controller that cuts a lot of corners.
 * */
template <class IMPL, class CACHE, class NEXT_CONTROL>
class FastCacheControl
{
public:
    /*
     * This mimics `IOBus`.
     * */
    struct IO 
    {
        FastCacheControl*  owner_;
        IOBus::out_queue_t outgoing_queue_;

        bool add_incoming(
    };
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_FAST_CONTROL_h
