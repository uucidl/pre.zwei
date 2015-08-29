#pragma once

namespace algos
{
// <concepts...
#define BidirectionalIterator typename
#define BinaryOperation typename
#define BinaryPredicate typename
#define Container typename
#define ForwardIterator typename
#define InputIterator typename
#define Integral typename
#define Iterator typename
#define OutputIterator typename
#define Readable typename
#define UnaryFunction typename
#define UnaryPredicate typename
#define UnaryPredicate typename
#define Writable typename
// ..concepts>

template <Readable R> struct ReadableConcept {
        using value_type = void;
};
template <Readable R> typename ReadableConcept<R>::value_type source(R x);

template <Iterator It> It successor(It x);

template <BidirectionalIterator It> It predecessor(It x);

template <Iterator It> struct IteratorConcept {
        using difference_type = void;
};

// # Container concept
template <Container C> struct ContainerConcept {
        using iterator_type = void;
        using const_iterator_type = void;
};

template <Iterator It, Container C>
typename ContainerConcept<C>::iterator_type begin(C &x);
template <Iterator It, Container C>
typename ContainerConcept<C>::const_iterator_type begin(C &x);
template <Iterator It, Container C>
typename ContainerConcept<C>::iterator_type end(C &x);
template <Iterator It, Container C>
typename ContainerConcept<C>::const_iterator_type end(C &x);

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
