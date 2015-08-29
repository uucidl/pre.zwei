// TODO(nicolas) let me chain parsers/processors on buffer. How do I
// do that?  this is to avoid having to create n streams for n
// processors.  also think that next() represents I/O, so it should be
// split from processing.

#include <cstddef>

enum BufferRangeErrorCode {
        /// no error has occured
        BR_NoError,
        /// the consumer attempted to read past the end
        BR_ReadPastEnd,
        /// unrecoverable IO error
        BR_IOError,
};

extern void
stream_on_memory(struct BufferRange *range, uint8_t *mem, size_t const size);

// NOTE(nicolas) a BufferRange shows the two aspect of processing data
// for I/O in one datastructure. The points of memory between start
// and end represent what can be processed by the CPU.
//
// The `next` function represents I/O.

struct BufferRange {
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

        enum BufferRangeErrorCode error;

        /**
         * Refill function, called when more data is needed by the writer/reader
         *
         * - pre-condition: cursor == end
         * - post-condition: start == cursor < end
         */
        enum BufferRangeErrorCode (*next)(struct BufferRange *);
};
