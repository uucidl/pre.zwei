#pragma once

#include "algos_concepts_define_typenames.ipp"

namespace algos
{
template <Readable R> struct ReadableConcept {
        using value_type = void;
};
template <Readable R> typename ReadableConcept<R>::value_type source(R x);

template <Iterator It> It successor(It x);

template <BidirectionalIterator It> It predecessor(It x);

template <Iterator It> struct IteratorConcept {
        using difference_type = void;
};

// # Sequence concept
template <Sequence C> struct SequenceConcept {
        using iterator_type = void;
        using const_iterator_type = void;
};

template <Iterator It, Sequence C>
typename SequenceConcept<C>::iterator_type begin(C &x);
template <Iterator It, Sequence C>
typename SequenceConcept<C>::const_iterator_type begin(C &x);
template <Iterator It, Sequence C>
typename SequenceConcept<C>::iterator_type end(C &x);
template <Iterator It, Sequence C>
typename SequenceConcept<C>::const_iterator_type end(C &x);

// # Writable concept
template <Writable W> struct WritableConcept {
        using value_type = void;
};

template <Writable W> typename WritableConcept<W>::value_type &sink(W &x);

// # BinaryOperation concept
template <BinaryOperation Op> struct BinaryOperationConcept {
        using type = void;
};
}

#include "algos_concepts_undef_typenames.ipp"
