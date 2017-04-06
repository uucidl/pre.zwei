#include "zwei_iobuffer.hpp"
#include "zwei_inlines.hpp"
#include "zwei_iobuffer_inlines.hpp"

zw_internal enum BufferedReaderError
iobuffer_memory_reader_refill(BufferedReader *range)
{
        // memory is read all at once, so asking for more is instantly past
        // the end.
        return fail(range, BufferedReaderError_ReadPastEnd);
}

zw_internal void iobuffer_read_memory(BufferedReader *range,
                                      uint8_t const *mem,
                                      size_t const size)
{
        range->start = mem;
        range->cursor = mem;
        range->end = range->start + size;
        range->error = BufferedReaderError_NoError;
        range->refill = iobuffer_memory_reader_refill;
}
