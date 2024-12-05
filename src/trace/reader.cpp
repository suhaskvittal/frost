/*
 *  author: Suhas Vittal
 *  date:   26 November 2024
 * */

#include "trace/reader.h"

#include <cstring>
#include <iostream>
#include <limits>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TraceReader::TraceReader(std::string filename)
    :filename_(filename),
    type_( filename.find(".xz") != std::string::npos ? TraceType::XZ : TraceType::GZ )
{
    if (type_ == TraceType::XZ) {
        xz_strm_ = LZMA_STREAM_INIT;
        if (lzma_stream_decoder(&xz_strm_, std::numeric_limits<uint64_t>::max(), LZMA_CONCATENATED) != LZMA_OK) {
            std::cerr << "xz: failed to initialize decoder.\n";
            exit(1);
        }
        xz_fin_ = fopen(filename.c_str(), "r");
        xz_buf_ = new char[CHUNK_SIZE];
        // Initialize input buffer
        xz_get_next_chunk();
        xz_strm_.next_in = (uint8_t*)xz_buf_;
        xz_strm_.avail_in = CHUNK_SIZE;
    } else {
        gz_fin_ = gzopen(filename.c_str(), "r");
    }
}

TraceReader::~TraceReader()
{
    if (type_ == TraceType::XZ) {
        lzma_end(&xz_strm_);
        fclose(xz_fin_);
        delete[] xz_buf_;
    } else {
        gzclose(gz_fin_);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TraceData
TraceReader::read()
{
    memset(&blk_, 0, sizeof(blk_));
    if (type_ == TraceType::XZ) xz_read();
    else                        gz_read();
    return TraceData(blk_);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
TraceReader::gz_read()
{
    gzread(gz_fin_, &blk_, sizeof(blk_));
    if (gzeof(gz_fin_)) {
        eof_ = true;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
handle_xz_error(lzma_ret r)
{
    std::cerr << "xz: ";
    switch (r) {
    case LZMA_MEM_ERROR:
        std::cerr << "memory allocation failed.\n";
        break;
    case LZMA_FORMAT_ERROR:
        std::cerr << "input is not in the .xz format.\n";
        break;
    case LZMA_OPTIONS_ERROR:
        std::cerr << "unsupported compression options.\n";
        break;
    case LZMA_DATA_ERROR:
        std::cerr << "data is corrupted.\n";
        break;
    case LZMA_BUF_ERROR:
        std::cerr << "compressed file is truncated or corrupt.\n";
        break;
    default:
        std::cerr << "unknown error: " << r << "\n";
    }
    exit(1);
}

void
TraceReader::xz_read()
{
    xz_strm_.next_out = (uint8_t*)&blk_;
    xz_strm_.avail_out = sizeof(blk_);

    while (xz_strm_.avail_out > 0) {
        if (xz_strm_.avail_in == 0 && !eof_) {
            xz_get_next_chunk();
        }
        lzma_ret r = lzma_code(&xz_strm_, eof_ ? LZMA_FINISH : LZMA_RUN);
        if (r != LZMA_OK) {
            if (r == LZMA_STREAM_END)
                return;
            else
                handle_xz_error(r);
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
TraceReader::xz_get_next_chunk()
{
    // Read next 1K bytes from file. 
    size_t num_bytes_read = fread(xz_buf_, 1, CHUNK_SIZE, xz_fin_);
    if (ferror(xz_fin_)) {
        std::cerr << "xz: read error " << strerror(errno) << "\n";
        exit(1);
    }
    xz_strm_.next_in = (uint8_t*)xz_buf_;
    xz_strm_.avail_in = num_bytes_read;
    eof_ = feof(xz_fin_);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
