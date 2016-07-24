#include "zwei_iobuffer.hpp"
#include "zwei_inlines.hpp"
#include "zwei_iobuffer_inlines.hpp"

zw_internal enum IOBufferIteratorError
fill_next_on_memory_buffer(IOBufferIterator *range)
{
        return fail(range, IOBufferIteratorError_ReadPastEnd);
}

void stream_on_memory(IOBufferIterator *range, uint8_t *mem, size_t const size)
{
        range->start = mem;
        range->cursor = mem;
        range->end = range->start + size;
        range->error = IOBufferIteratorError_NoError;
        range->fill_next = fill_next_on_memory_buffer;
}
