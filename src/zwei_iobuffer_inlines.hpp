// for when you want to write your own BufferRange implementations

#pragma once

zw_internal inline enum BufferRangeErrorCode next_zeros(struct BufferRange *range)
{
        zw_local_persist uint8_t const zeros[256] = {0};

        range->start = (uint8_t *)zeros;
        range->cursor = (uint8_t *)zeros;
        range->end = (uint8_t *)zeros + sizeof(zeros);

        return range->error;
}

zw_internal inline enum BufferRangeErrorCode fail(struct BufferRange *range,
                                           enum BufferRangeErrorCode error)
{
        range->error = error;
        range->next = next_zeros;

        return range->next(range);
}
