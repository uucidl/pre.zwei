#pragma once

#include "zwei_inlines.hpp"

/** @file: buffer-centric IO.
 *
 * @url: https://fgiesen.wordpress.com/2011/11/21/buffer-centric-io/
 */

#include <cstdint>

enum BufferedReaderError {
        /// no error has occured
        BufferedReaderError_NoError,

        /// the consumer attempted to read past the end
        BufferedReaderError_ReadPastEnd,

        /// unrecoverable IO error
        BufferedReaderError_ReadError,

        /// unrecoverable decoding error
        BufferedReaderError_DecodingError,
};

/**
 * Buffer is owned and defined by the producer process, not the consumer.
 */
struct BufferedReader {
        uint8_t const *start;

        // end >= start
        uint8_t const *end;

        // start <= cursor <= end
        uint8_t const *cursor;

        enum BufferedReaderError error;

        /**
         * Consumer calls this to refill the buffer.
         *
         * @pre: cursor == end @{buffer entirely consumed}
         * @post: start == cursor < end
         */
        enum BufferedReaderError (*refill)(BufferedReader *);
};

zw_internal void iobuffer_read_memory(BufferedReader *iobuffer,
                                      uint8_t const *data,
                                      size_t data_size);
static inline void iobuffer_read_memory(BufferedReader *iobuffer,
                                        uint8_t const *data_first,
                                        uint8_t const *data_last)
{
        return iobuffer_read_memory(iobuffer, data_first,
                                    data_last - data_first);
}
