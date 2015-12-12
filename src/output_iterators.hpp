#pragma once

#include "algos_concepts_define_typenames.ipp"
namespace algos
{

/**
   A generic sink (type that supports assignment) which discards its inputs.
*/
struct NullSink {
        template <typename T> NullSink &operator=(const T &x) { return *this; }
};

/**
   Adapter for another iterator that will disregard writes made to it
*/
template <OutputIterator I> struct NullOutputIteratorAdapter {
        using WritableMe = NullOutputIteratorAdapter;
        using OutputIteratorMe = NullOutputIteratorAdapter;

        I iterator;
        NullSink null_sink;

        friend NullSink &sink(WritableMe &self) { return self.null_sink; }

        friend OutputIteratorMe successor(const OutputIteratorMe &x)
        {
                auto y = x;
                y.iterator = successor(y.iterator);
                return y;
        }

        /** @requires RandomAccessIterator(I) */
        friend typename IteratorConcept<I>::difference_type
        operator-(NullOutputIteratorAdapter const &x,
                  NullOutputIteratorAdapter const &y)
        {
                return x.iterator - y.iterator;
        }
};

template <OutputIterator I>
struct WritableConcept<NullOutputIteratorAdapter<I>> {
        using value_type = typename WritableConcept<I>::value_type;
};

template <OutputIterator I>
struct IteratorConcept<NullOutputIteratorAdapter<I>> {
        using difference_type = typename IteratorConcept<I>::difference_type;
};
}
#include "algos_concepts_undef_typenames.ipp"
