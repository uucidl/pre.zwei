#pragma once

// TAG(generic)

#include "algos_concepts.hpp"

#include <cstddef> // for ptrdiff_t
#include <utility> // for pair

// TODO(nicolas): compare / merge with things from eop

namespace algos
{
template <typename T> T assign(T &x, T const y)
{
        x = y;
        return x;
}

// @require: TotallyOrdered(T)
template <typename T> const T &max(const T &a, const T &b)
{
        if (b < a) {
                return a;
        }
        return b;
}
}

// Integral types
namespace algos
{
template <Integral I> bool zero(I i) { return i == 0; };

template <Integral I> I successor(I i) { return i + 1; }

template <Integral I> I predecessor(I i) { return i - 1; }
}

// Pointer types
namespace algos
{
template <typename T> struct ReadableConcept<T *> {
        using value_type = T;
};

template <typename T> T *pointer(T &x) { return &x; }

// # pointer as Readable
template <typename T> const T &source(const T *x) { return *x; }

template <typename T> T &source(T *x) { return *x; }

// # pointer as Iterator

template <typename T> struct IteratorConcept<T *> {
        using difference_type = std::ptrdiff_t;
};

template <typename T> const T *successor(const T *x) { return x + 1; }

template <typename T> T *successor(T *x) { return x + 1; }

// # pointer as BidirectionalIterator

template <typename T> const T *predecessor(const T *x) { return x - 1; }

template <typename T> T *predecessor(T *x) { return x - 1; }

// # pointer as Writable
template <typename T> T &sink(T *x) { return *x; }
}

// Array types
namespace algos
{
template <typename T, size_t N> struct SequenceConcept<T (&)[N]> {
        using iterator_type = T *;
        using const_iterator_type = T const *;
};

template <typename T, size_t N> constexpr T *begin(T (&array)[N])
{
        return array;
}

template <typename T, size_t N> constexpr T *end(T (&array)[N])
{
        return array + N;
}
}

// # algorithms on ranges.
//
// Ranges can be bounded [first, last), counted [[first, count)) and #
// guarded [first, <sentinel>) ranges.
namespace algos
{

template <typename I> bool range_empty(I first, I last)
{
        return first == last;
}

template <Iterator I, Iterator J>
J advance_until(I f,
                typename ReadableConcept<I>::value_type sentinel,
                J counter)
{
        while (sentinel != source(f)) {
                counter = successor(counter);
                f = successor(f);
        }

        return counter;
}

template <Iterator I>
I bound_guarded(I f, typename ReadableConcept<I>::value_type sentinel)
{
        return advance_until(f, sentinel, f);
}

template <Iterator I>
typename IteratorConcept<I>::difference_type
count_guarded(I f, typename ReadableConcept<I>::value_type sentinel)
{
        return advance_until(f, sentinel,
                             typename IteratorConcept<I>::difference_type(0));
}

/**
 * @require: Domain(Pred) = ValueType(I)
 */
template <Iterator I, UnaryPredicate Pred> I find_if(I first, I last, Pred pred)
{
        while (first != last) {
                if (pred(source(first))) {
                        break;
                }
                first = successor(first);
        }

        return first;
}

/**
 * @require: Domain(Pred) = ValueType(I)
 */
template <Iterator I, Integral N, UnaryPredicate Pred>
std::pair<I, N> find_if_n(I first, N count, Pred pred)
{
        while (count) {
                if (pred(source(first))) {
                        break;
                }
                first = successor(first);
                count = predecessor(count);
        }

        return {first, count};
}

/**
 * @require: Domain(Pred) = ValueType(I)
 */
template <Iterator I, UnaryPredicate Pred>
I find_if_guarded(I first,
                  typename ReadableConcept<I>::value_type sentinel,
                  Pred pred)
{
        while (sentinel != source(first)) {
                if (pred(source(first))) {
                        break;
                }
                first = successor(first);
        }

        return first;
}

template <Iterator I>
I find_guarded(I first,
               typename ReadableConcept<I>::value_type sentinel,
               typename ReadableConcept<I>::value_type const &x)
{
        return find_if_guarded(
            first, sentinel,
            [x](typename ReadableConcept<I>::value_type const &y) {
                    return x == y;
            });
}

/**
 * @requires Domain(Pred) = ValueType(I)
 */
template <Iterator I, UnaryPredicate Pred>
std::pair<I, I> find_adjacent_if_guarded(
    I f0, typename ReadableConcept<I>::value_type sentinel, Pred pred)
{
        using ValueType = typename ReadableConcept<I>::value_type;
        auto const inverse_pred = [pred](ValueType const &x) {
                return !pred(x);
        };

        auto adjacent_first = find_if_guarded(f0, sentinel, pred);
        auto adjacent_last =
            find_if_guarded(adjacent_first, sentinel, inverse_pred);
        return {adjacent_first, adjacent_last};
}

/**
 * @require: Domain(Pred) = ValueType(It)
 */
template <Iterator I, UnaryPredicate Pred>
I find_last_if_guarded(I first,
                       typename ReadableConcept<I>::value_type sentinel,
                       Pred pred)
{
        I y_prime = find_if_guarded(first, sentinel, pred);
        if (sentinel == source(y_prime)) {
                return y_prime;
        }

        I y;
        do {
                y = y_prime;
                y_prime = find_if_guarded(successor(y), sentinel, pred);
        } while (sentinel != source(y_prime));

        return y;
}

template <Iterator It>
It find_last_guarded(It first,
                     typename ReadableConcept<It>::value_type sentinel,
                     typename ReadableConcept<It>::value_type const &x)
{
        return find_last_if_guarded(
            first, sentinel,
            [x](typename ReadableConcept<It>::value_type const &y) {
                    return x == y;
            });
}

/**
 * @require: Domain(Pred) = ValueType(It)
 */
template <BidirectionalIterator It, UnaryPredicate Pred>
It find_if_backward(It first, It last, Pred pred)
{
        while (first != last) {
                last = predecessor(last);
                if (pred(source(last))) {
                        return last;
                }
        }

        return last;
}

template <BidirectionalIterator It>
It find_backward(It first,
                 It last,
                 typename ReadableConcept<It>::value_type const &x)
{
        return find_if_backward(
            first, last,
            [x](typename ReadableConcept<It>::value_type const &y) {
                    return y == x;
            });
}

/**
 * @require: ValueType(I0) = ValueType(I1)
 */
template <InputIterator I0, InputIterator I1, BinaryPredicate R>
std::pair<I0, I1> find_mismatch_guarded(
    I0 f0, I1 f1, typename ReadableConcept<I0>::value_type sentinel, R r)
{
        while (sentinel != source(f0) && sentinel != source(f1) &&
               r(source(f0), source(f1))) {
                f0 = successor(f0);
                f1 = successor(f1);
        }

        return std::make_pair(f0, f1);
}

/**
 * @require: Domain(R) = ValueType(I0) = ValueType(I1)
 */
template <InputIterator I0, InputIterator I1, BinaryPredicate R>
std::pair<I0, I1> find_mismatch_bounded_guarded(
    I0 f0, I0 l0, I1 f1, typename ReadableConcept<I0>::value_type sentinel, R r)
{
        while (f0 != l0 && sentinel != source(f1) &&
               r(source(f0), source(f1))) {
                f0 = successor(f0);
                f1 = successor(f1);
        }
        return std::make_pair(f0, f1);
}

/**
 * @require: Domain(Op) = ValueType(I)
 */
template <Iterator I, UnaryFunction Op>
void for_each(I first, I last, Op operation)
{
        while (first != last) {
                operation(source(first));
                first = successor(first);
        }
}

/**
 * @require: Domain(Op) = ValueType(I)
 * @pre: n >= 0
 */
template <Iterator I, UnaryFunction Op>
I for_each_n(I first,
             typename IteratorConcept<I>::difference_type n,
             Op operation)
{
        while (!zero(n)) {
                operation(source(first));
                first = successor(first);
                n = predecessor(n);
        }

        return first;
}

template <Iterator I, OutputIterator O> void copy_step(I &f_i, O &f_o)
{
        sink(f_o) = source(f_i);
        f_o = successor(f_o);
        f_i = successor(f_i);
}

template <Iterator I, OutputIterator O> O copy(I first, I last, O dest_first)
{
        while (first != last) {
                copy_step(first, dest_first);
        }

        return dest_first;
}

template <Iterator InputIt, OutputIterator O>
O copy_n(InputIt first,
         typename IteratorConcept<InputIt>::difference_type count,
         O dest_first)
{
        while (count) {
                sink(dest_first) = source(first);
                first = successor(first);
                dest_first = successor(dest_first);
                count = predecessor(count);
        }

        return dest_first;
}

template <Iterator I, OutputIterator O>
std::pair<I, O> copy_to_bounded(I first, I last, O dest_first, O dest_last)
{
        while (first != last && dest_first != dest_last) {
                copy_step(first, dest_first);
        }
        return {first, dest_first};
}

template <Iterator I, OutputIterator O>
std::pair<I, O>
copy_guarded_to_bounded(I first,
                        typename ReadableConcept<I>::value_type sentinel,
                        O dest_first,
                        O dest_last)
{
        while (sentinel != source(first) && dest_first != dest_last) {
                copy_step(first, dest_first);
        }
        return std::make_pair(first, dest_first);
}

/**
 * @require: Domain(Fn) = ValueType(I)
 * @require: CoDomain(Fn) = ValueType(O)
 */
template <Iterator I, OutputIterator O, UnaryFunction Fn>
O apply_copy(I first, I last, O dest_first, Fn fn)
{
        while (first != last) {
                sink(dest_first) = fn(source(first));
                dest_first = successor(dest_first);
                first = successor(first);
        }

        return dest_first;
}

template <Iterator I, OutputIterator O, UnaryFunction Fn>
O apply_copy_n(I first,
               typename IteratorConcept<I>::difference_type count,
               O dest_first,
               Fn fn)
{
        while (count) {
                sink(dest_first) = fn(source(first));
                first = successor(first);
                dest_first = successor(dest_first);
                --count;
        }

        return dest_first;
}

/**
 * @require: Domain(Pred) = ValueType(I)
 * @require: Domain(Fn) = ValueType(I)
 * @require: CoDomain(Fn) = ValueType(O)
 */
template <Iterator I, OutputIterator O, UnaryPredicate Pred, UnaryFunction Fn>
O apply_copy_if(I first, I last, O dest_first, Fn fn, Pred pred)
{
        while (first != last) {
                if (pred(source(first))) {
                        sink(dest_first) = fn(source(first));
                        dest_first = successor(dest_first);
                }
                first = successor(first);
        }

        return dest_first;
}

template <Iterator I, OutputIterator O, UnaryFunction Fn, UnaryPredicate Pred>
std::pair<I, O> apply_copy_to_bounded_if(
    I first, I last, O dest_first, O dest_last, Fn fn, Pred pred)
{
        while (first != last && dest_first != dest_last) {
                if (pred(source(first))) {
                        sink(dest_first) = fn(source(first));
                        dest_first = successor(dest_first);
                }
                first = successor(first);
        }
        return {first, dest_first};
}
}

// binary operations
namespace algos
{
// function pointers
template <typename T> struct BinaryOperationConcept<T &(*)(T &, T &)> {
        using type = T;
};
}

namespace algos
{
template <typename T> struct maximum {
        T &operator()(T &x, T &y) const { return max(x, y); }

        T const &operator()(T const &x, T const &y) const { return max(x, y); }
};

template <typename T> struct BinaryOperationConcept<maximum<T>> {
        using type = T;
};

/**
 * apply `fun` to each element in [first, last) and reduce using `op`
 * @pre: !range_empty(first, last)
 */
template <Iterator I, BinaryOperation Op, UnaryFunction F>
typename BinaryOperationConcept<Op>::type
apply_reduce_nonempty(I first, I last, F fun, Op op)
{
        typename BinaryOperationConcept<Op>::type result = fun(first);
        first = successor(first);
        while (first != last) {
                result = op(result, fun(first));
                first = successor(first);
        }

        return result;
}

/**
 * apply `fun` to each element in [first, last) and reduce using `op`
 * @pre: valid_range(first,last)
 */
template <Iterator I, BinaryOperation Op, UnaryFunction F>
typename BinaryOperationConcept<Op>::type
apply_reduce(I first,
             I last,
             F fun,
             Op op,
             typename BinaryOperationConcept<Op>::type zero)
{
        if (first == last) {
                return zero;
        }
        return apply_reduce_nonempty(first, last, fun, op);
}
}

// derived sequence algorithms
namespace algos
{

template <Sequence S> bool empty(S s) { return range_empty(begin(s), end(s)); }

/**
 * TAG(degenerate) find_if_n
 * @requires: Domain(pred) == ValueType(I)
 */
template <Iterator I, Integral N, UnaryPredicate P>
bool all_n(I first, N count, P pred)
{
        using ValueType = typename ReadableConcept<I>::value_type;
        return find_if_n(first, count,
                         [&](ValueType const &x) { return !pred(x); })
                   .second == N(0);
}

template <InputIterator I0, InputIterator I1>
bool equal_n_m(I0 f0,
               typename IteratorConcept<I0>::difference_type n0,
               I1 f1,
               typename IteratorConcept<I1>::difference_type n1)
{
        while (n0 && n1 && source(f0) == source(f1)) {
                --n0;
                --n1;
        }
        return n0 == 0 && n1 == 0;
}

template <Iterator I0, Iterator I1, BinaryPredicate R>
bool equal_bounded_guarded(I0 f0,
                           I0 l0,
                           I1 f1,
                           typename ReadableConcept<I0>::value_type sentinel,
                           R relation)
{
        auto mismatch =
            find_mismatch_bounded_guarded(f0, l0, f1, sentinel, relation);
        return mismatch.first == l0 && sentinel == source(mismatch.second);
}

/**
 * @require: Domain(P) == ValueType(I0) == ValueType(I1)
 */
template <Iterator I0, Iterator I1, BinaryPredicate R>
bool equal_guarded(I0 f0,
                   I1 f1,
                   typename ReadableConcept<I0>::value_type sentinel,
                   R relation)
{
        auto mismatch = find_mismatch_guarded(f0, f1, sentinel, relation);
        return sentinel == source(mismatch.first) &&
               sentinel == source(mismatch.second);
}
}

// void output iterators
namespace algos
{

/**
 * A generic sink (type that supports assignment) which discards any input
 *
 * @model: Sink
 */
struct VoidSink {
        template <typename T> VoidSink const &operator=(const T &x) const
        {
                return *this;
        }
};

/**
 * Adapts an output iterator as an output that will disregard writes made to it.
 *
 * @model: OutputIterator
 */
template <typename O> struct VoidOutputIteratorAdapter {
        using WritableMe = VoidOutputIteratorAdapter;
        using OutputIteratorMe = VoidOutputIteratorAdapter;

        O iterator;

        // @model: Writable
        friend VoidSink const &sink(WritableMe &self)
        {
                static constexpr VoidSink void_sink{};
                return void_sink;
        }

        // @model: Iterator
        friend OutputIteratorMe successor(const OutputIteratorMe &x)
        {
                return {successor(x.iterator)};
        }

        // @require: RandomAccessIterator(O)
        friend typename IteratorConcept<O>::difference_type
        operator-(VoidOutputIteratorAdapter const &x,
                  VoidOutputIteratorAdapter const &y)
        {
                return x.iterator - y.iterator;
        }
};

template <typename I> struct WritableConcept<VoidOutputIteratorAdapter<I>> {
        using value_type = typename WritableConcept<I>::value_type;
};

template <typename I> struct IteratorConcept<VoidOutputIteratorAdapter<I>> {
        using difference_type = typename IteratorConcept<I>::difference_type;
};
}

// tree concepts and algorithms
namespace algos
{
#define TreeCoordinate typename

template <typename T> struct TreeCoordinateConcept {
        using WeightType = void;
};

template <TreeCoordinate C> C descendants_begin(C c);
template <TreeCoordinate C> C descendants_end(C c);
}

namespace algos
{
template <TreeCoordinate C>
typename TreeCoordinateConcept<C>::WeightType weight_recursive(C c)
{
        using N = typename TreeCoordinateConcept<C>::WeightType;
        N weight(0);

        auto first = descendants_begin(c);
        auto last = descendants_end(c);

        while (first != last) {
                weight = weight + weight_recursive(source(first));
                first = successor(first);
        }
        return successor(weight);
}

template <TreeCoordinate C>
typename TreeCoordinateConcept<C>::WeightType height_recursive(const C &c)
{
        using N = typename TreeCoordinateConcept<C>::WeightType;

        N height(0);

        auto first = descendants_begin(c);
        auto last = descendants_end(c);
        while (first != last) {
                auto branch_height = height_recursive(source(first));
                height = max(height, branch_height);
                first = successor(first);
        }

        return successor(height);
}

enum VisitType { PRE, POST };
template <TreeCoordinate C, typename Proc>
Proc traverse_nonempty(const C &c, Proc proc)
{
        proc(PRE, c);

        auto first = descendants_begin(c);
        auto last = descendants_end(c);
        while (first != last) {
                proc = traverse_nonempty(source(first), proc);
                first = successor(first);
        }

        proc(POST, c);
        return proc;
}
}

namespace algos
{

template <TreeCoordinate C, typename Proc> struct visit_element {
        Proc proc;

        visit_element(Proc proc) : proc(proc) {}
        visit_element(visit_element const &) = default;
        visit_element &operator=(visit_element const &) = default;

        void operator()(VisitType visit, C c)
        {
                if (visit != PRE)
                        return;
                proc(source(c));
        }
};

template <TreeCoordinate C, typename Proc>
void traverse_each(C const &c, Proc proc)
{
        traverse_nonempty(c, visit_element<C, Proc>(proc));
}
};
