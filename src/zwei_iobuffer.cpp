#include "zwei_iobuffer.hpp"
#include "zwei_inlines.hpp"
#include "zwei_iobuffer_inlines.hpp"

zw_internal enum BufferRangeErrorCode
next_on_memory_buffer(struct BufferRange *range)
{
        return fail(range, BR_ReadPastEnd);
}

void stream_on_memory(struct BufferRange *range,
                      uint8_t *mem,
                      size_t const size)
{
        range->start = mem;
        range->cursor = mem;
        range->end = range->start + size;
        range->error = BR_NoError;
        range->next = next_on_memory_buffer;
}
