/* author: Suhas Vittal
 *  date:   26 November 2024
 * */

#ifndef TRACE_READER_h
#define TRACE_READER_h

#include <cstdio>
#include <string>
#include <string_view>

#include <lzma.h>
#include <zlib.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

enum class TraceType { XZ, GZ };

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class TRACE_FORMAT>
class TraceReader
{
public:
    constexpr static size_t CHUNK_SIZE = 8192;

    const std::string_view filename_;
    const TraceType type_;

    bool eof_ =false;
private:
    /*
     * File readers for gz and xz
     * */
    gzFile gz_fin_;

    lzma_stream xz_strm_;
    FILE*       xz_fin_;
    char*       xz_buf_;

    TRACE_FORMAT blk_;
public:
    TraceReader(std::string);
    ~TraceReader(void);

    TRACE_FORMAT& operator()(void);
private:
    void gz_read(void);
    void xz_read(void);

    void xz_get_next_chunk(void);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "reader.tpp"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // TRACE_READER_h

