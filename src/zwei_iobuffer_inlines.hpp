// for when you want to write your own IOBufferIterator implementations

#pragma once

zw_internal inline enum IOBufferIteratorError
fill_zeros(IOBufferIterator *range)
{
        zw_local_persist uint8_t const zeros[256] = {0};

        range->start = (uint8_t *)zeros;
        range->cursor = (uint8_t *)zeros;
        range->end = (uint8_t *)zeros + sizeof(zeros);

        return range->error;
}

zw_internal inline enum IOBufferIteratorError
fail(IOBufferIterator *range, enum IOBufferIteratorError error)
{
        range->error = error;
        range->fill_next = fill_zeros;

        return range->fill_next(range);
}

zw_internal inline IOBufferIteratorError refill_iobuffer(IOBufferIterator *x)
{
        return x->fill_next(x);
}

zw_internal inline IOBufferIteratorError
finish_iobuffer(IOBufferIterator *iobuffer)
{
        while (iobuffer->error == IOBufferIteratorError_NoError) {
                refill_iobuffer(iobuffer);
        }
        return iobuffer->error;
}
