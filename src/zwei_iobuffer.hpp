#pragma once

// TODO(nicolas) let me chain parsers/processors on buffer. How do I
// do that?  this is to avoid having to create n streams for n
// processors.  also think that next() represents I/O, so it should be
// split from processing.

#include <cstdint>

enum IOBufferIteratorError {
        /// no error has occured
        IOBufferIteratorError_NoError,
        /// the consumer attempted to read past the end
        IOBufferIteratorError_ReadPastEnd,
        /// unrecoverable IO error
        IOBufferIteratorError_IOError,
        /// If the stream is a decoder stream, denotes a decoding problem
        IOBufferIteratorError_DecodingError,
};

struct IOBufferIterator;

extern void
stream_on_memory(IOBufferIterator *range, uint8_t *mem, size_t const size);

// NOTE(nicolas) a IOBufferIterator shows the two aspect of processing data
// for I/O in one datastructure. The points of memory between start
// and end represent what can be processed by the CPU.
//
// The `next` function represents I/O.

// TODO(nicolas): the name buffer range isn't really great. When using
// STL's concept, it corresponds more to a specific model for an InputIterator
// for I/O and decoding.
//
// This doesn't mean we should make it STL compatible. At least awareness that
// we're doing io in next seems rather important to preserve.

struct IOBufferIterator {
        /**
         * start of buffer.
         */
        uint8_t *start;

        /**
         * one byte past the end of buffer.
         *
         * so that:
         * - size = end - start
         * - end >= start
         */
        uint8_t *end;

        /**
         * interesting point in the buffer
         *
         * start <= cursor <= end
         */
        uint8_t *cursor;

        enum IOBufferIteratorError error;

        /**
         * Refill function, called when more data is needed by the writer/reader
         *
         * - pre-condition: cursor == end
         * - post-condition: start == cursor < end
         */
        enum IOBufferIteratorError (*fill_next)(IOBufferIterator *);
};
