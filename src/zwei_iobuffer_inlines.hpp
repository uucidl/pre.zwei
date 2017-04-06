// for when you want to write your own BufferedReader implementations

#pragma once

zw_internal inline enum BufferedReaderError fill_zeros(BufferedReader *range)
{
        zw_local_persist uint8_t const zeros[256] = {0};

        range->start = (uint8_t *)zeros;
        range->cursor = (uint8_t *)zeros;
        range->end = (uint8_t *)zeros + sizeof(zeros);

        return range->error;
}

zw_internal inline enum BufferedReaderError fail(BufferedReader *range,
                                                 enum BufferedReaderError error)
{
        range->error = error;
        range->refill = fill_zeros;
        return range->refill(range);
}

zw_internal inline BufferedReaderError refill_iobuffer(BufferedReader *x)
{
        return x->refill(x);
}

zw_internal inline BufferedReaderError finish_iobuffer(BufferedReader *iobuffer)
{
        while (iobuffer->error == BufferedReaderError_NoError) {
                refill_iobuffer(iobuffer);
        }
        return iobuffer->error;
}
